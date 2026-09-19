#include "Server.h"
#include "EventLoop.h"
#include "Logger.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>

using namespace std;

int Server::listen_fd = -1;

Server::Server(
    int port,
    const string& ip,
    CanRuntime* can_runtime)
    : port_(port),
      ip_(ip),
      event_loop(nullptr),
      can_runtime_(can_runtime)
{
    memset(&server_addr_, 0, sizeof(server_addr_));
}

Server::~Server()
{
    if(listen_fd != -1)
    {
        close(listen_fd);
        LOG_INFO("listening socket closed, fd=" + to_string(listen_fd));
    }
    delete event_loop;
}

int Server::createListenfd()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd == -1)
    {
        LOG_ERROR("socket create failed: " + string(strerror(errno)));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
    return fd;
}

void Server::setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags == -1)
    {
        LOG_ERROR("set non-blocking failed: " + string(strerror(errno)));
        return;
    }

    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    LOG_INFO("socket(fd=" + to_string(fd) + ") set to non-blocking");
}

bool Server::init()
{
    listen_fd = createListenfd();
    if(listen_fd == -1)
    {
        return false;
    }

    setNonBlocking(listen_fd);

    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port_);
    server_addr_.sin_addr.s_addr = inet_addr(ip_.c_str());

    if(bind(listen_fd, (struct sockaddr*)&server_addr_, sizeof(server_addr_)) == -1)
    {
        LOG_ERROR("bind port " + to_string(port_) + " failed: " + string(strerror(errno)));
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    if(listen(listen_fd, 128) == -1)
    {
        LOG_ERROR("listen on port " + to_string(port_) + " failed: " + string(strerror(errno)));
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    event_loop = new EventLoop(can_runtime_);
    event_loop->addEvent(listen_fd, EPOLLIN, nullptr);
    LOG_INFO("server initialized successfully, listening on " + ip_ + ":" + to_string(port_));
    return true;
}

void Server::start()
{
    if(event_loop)
    {
        event_loop->setThreadPool(4);
        event_loop->loop();
    }
}
