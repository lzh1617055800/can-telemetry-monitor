#include "EventLoop.h"
#include "Server.h"
#include "Connection.h"
#include "Logger.h"
#include "ThreadPool.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <utility>
#include <sys/epoll.h>

using namespace std;

EventLoop* EventLoop::current_loop_ = nullptr;
volatile sig_atomic_t EventLoop::stop_flag_ = 0;

EventLoop::EventLoop(CanRuntime* can_runtime)
    : can_runtime_(can_runtime)
{
    epoll_fd = epoll_create(1024);
    if(epoll_fd == -1)
    {
        LOG_ERROR("epoll create failed: " + string(strerror(errno)));
        exit(EXIT_FAILURE);
    }
    events.resize(MAX_EVENTS);
    current_loop_ = this;
    LOG_INFO("epoll created successfully (epoll_fd=" + to_string(epoll_fd) + ")");
}

EventLoop::~EventLoop()
{
    close(epoll_fd);
    lock_guard<mutex> lock(conn_mutex_);
    for(auto& pair : conn_map)
    {
        close(pair.first);
    }
    conn_map.clear();
    current_loop_ = nullptr;
}

void EventLoop::addEvent(
    int fd,
    uint32_t event,
    shared_ptr<Connection> conn)
{
    lock_guard<mutex> lock(conn_mutex_);

    struct epoll_event ep_event;
    memset(&ep_event, 0, sizeof(ep_event));
    ep_event.data.fd = fd;
    ep_event.events = event | EPOLLET;

    if(conn_map.find(fd) != conn_map.end())
    {
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ep_event);
    }
    else
    {
        conn_map.emplace(fd, std::move(conn));
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ep_event);
        LOG_INFO("fd=" + to_string(fd) + " registered, event=" + to_string(event));
    }
}

void EventLoop::updateEvent(
    int fd,
    uint32_t event,
    Connection* expected)
{
    {
        lock_guard<mutex> lock(conn_mutex_);
        auto it = conn_map.find(fd);
        if(it == conn_map.end() ||
           (expected != nullptr && it->second.get() != expected))
        {
            return;
        }
    }

    struct epoll_event ep_event;
    memset(&ep_event, 0, sizeof(ep_event));
    ep_event.data.fd = fd;
    ep_event.events = event | EPOLLET;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ep_event);
}

void EventLoop::removeEvent(int fd, Connection* expected)
{
    lock_guard<mutex> lock(conn_mutex_);
    auto it = conn_map.find(fd);
    if(it == conn_map.end())
    {
        return;
    }

    if(expected != nullptr && it->second.get() != expected)
    {
        return;
    }

    timer_.removeTimer(fd);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    conn_map.erase(it);
    close(fd);
    LOG_INFO("fd=" + to_string(fd) + " connection closed and removed");
}

bool EventLoop::isRegistered(
    int fd,
    const Connection* expected) const
{
    lock_guard<mutex> lock(conn_mutex_);
    auto it = conn_map.find(fd);
    return it != conn_map.end() &&
           (expected == nullptr || it->second.get() == expected);
}

void EventLoop::loop()
{
    LOG_INFO("event loop started");
    while(!stop_flag_)
    {
        int num_events = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, 100);
        if(num_events == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }
            LOG_ERROR("epoll_wait failed: " + string(strerror(errno)));
            break;
        }

        auto expired_fds = timer_.tick();
        for(auto& fd : expired_fds)
        {
            shared_ptr<Connection> connection;
            {
                lock_guard<mutex> lock(conn_mutex_);
                auto connection_it = conn_map.find(fd);
                if(connection_it != conn_map.end())
                {
                    connection = connection_it->second;
                }
            }

            if(connection != nullptr && connection->isSse())
            {
                timer_.addTimer(fd, TIMEOUT_SEC);
                continue;
            }

            LOG_INFO("fd=" + to_string(fd) + " timed out");
            removeEvent(fd);
        }

        dispatchSseEvents();
        handleEvents(num_events);
    }

    LOG_INFO("server is shutting down");
}

void EventLoop::dispatchSseEvents()
{
    vector<pair<int, shared_ptr<Connection>>> connections;
    {
        lock_guard<mutex> lock(conn_mutex_);
        connections.reserve(conn_map.size());
        for(const auto& item : conn_map)
        {
            connections.emplace_back(item.first, item.second);
        }
    }

    vector<pair<int, Connection*>> overflowed_connections;

    for(const auto& item : connections)
    {
        const int fd = item.first;
        const shared_ptr<Connection>& connection = item.second;

        if(connection == nullptr || !connection->isSse())
        {
            continue;
        }

        if(connection->pumpSse())
        {
            updateEvent(fd, EPOLLIN | EPOLLOUT, connection.get());
        }

        if(connection->sseOverflowed())
        {
            overflowed_connections.emplace_back(fd, connection.get());
        }
    }

    for(const auto& item : overflowed_connections)
    {
        const int fd = item.first;
        LOG_ERROR("SSE client exceeded pending buffer limit, fd=" +
                  to_string(fd));
        removeEvent(fd, item.second);
    }
}

void EventLoop::handleEvents(int num_events)
{
    for(int i = 0; i < num_events; i++)
    {
        int fd = events[i].data.fd;
        uint32_t revents = events[i].events;

        if(fd == Server::getListenfd())
        {
            handleNewConnection(fd);
        }
        else if(revents & (EPOLLIN | EPOLLPRI | EPOLLOUT))
        {
            shared_ptr<Connection> conn;
            {
                lock_guard<mutex> lock(conn_mutex_);
                auto it = conn_map.find(fd);
                if(it == conn_map.end())
                {
                    continue;
                }
                conn = it->second;
            }

            timer_.updateTimer(fd, TIMEOUT_SEC);
            if(thread_pool_)
            {
                thread_pool_->submit([this, conn, revents]
                {
                    handleIOEvent(conn.get(), revents);
                });
            }
            else
            {
                handleIOEvent(conn.get(), revents);
            }
        }
        else if(revents & EPOLLERR)
        {
            LOG_ERROR("fd=" + to_string(fd) + " error event");
            removeEvent(fd);
        }
    }
}

void EventLoop::handleNewConnection(int listen_fd)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while(true)
    {
        int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if(conn_fd == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            LOG_ERROR("accept failed: " + string(strerror(errno)));
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        LOG_INFO("new connection " + string(client_ip) + ":" + to_string(client_port) +
                 " (conn_fd=" + to_string(conn_fd) + ")");

        int flags = fcntl(conn_fd, F_GETFL, 0);
        fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK);

        auto conn = make_shared<Connection>(
            conn_fd,
            this,
            can_runtime_);
        addEvent(conn_fd, EPOLLIN, conn);
        timer_.addTimer(conn_fd, TIMEOUT_SEC);
    }
}

void EventLoop::handleIOEvent(Connection* conn, uint32_t revents)
{
    if(!conn)
    {
        return;
    }

    int fd = conn->getFd();

    if(revents & EPOLLERR)
    {
        LOG_ERROR("fd=" + to_string(fd) + " error event");
        removeEvent(fd, conn);
        return;
    }

    if(revents & (EPOLLIN | EPOLLPRI))
    {
        conn->handleRead();
    }

    if((revents & EPOLLOUT) != 0 && isRegistered(fd, conn))
    {
        conn->handleWrite();
    }
}

void EventLoop::signalHandler(int signum)
{
    (void)signum;
    stop_flag_ = 1;
}

void EventLoop::setThreadPool(int num_threads)
{
    thread_pool_ = make_unique<ThreadPool>(num_threads);
}
