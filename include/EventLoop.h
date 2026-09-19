#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H
#include "Timer.h"
#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include <csignal>
#include <memory>
#include <mutex>
using namespace std;

class Connection;
class ThreadPool;
class CanRuntime;
class EventLoop
{
public:
    explicit EventLoop(CanRuntime* can_runtime = nullptr);
    ~EventLoop();
    void loop();
    void addEvent(
        int fd,
        uint32_t event,
        std::shared_ptr<Connection> conn);
    void updateEvent(
        int fd,
        uint32_t event,
        Connection* expected = nullptr);
    void removeEvent(int fd, Connection* expected = nullptr);
    bool isRegistered(int fd, const Connection* expected) const;
    static EventLoop* getCurrentLoop(){return current_loop_;}
    static void signalHandler(int signum);
    void setThreadPool(int num_threads);

private:
    void handleEvents(int num_events);
    void handleNewConnection(int lishen_fd);
    void handleIOEvent(Connection* conn,uint32_t revents);
    void dispatchSseEvents();
    int epoll_fd;
    vector<struct epoll_event> events;
    unordered_map<int,std::shared_ptr<Connection>> conn_map;
    mutable std::mutex conn_mutex_;
    static const int MAX_EVENTS = 1024;
    static EventLoop* current_loop_;
    Timer timer_;
    static const int TIMEOUT_SEC = 15;
    static volatile sig_atomic_t stop_flag_;
    unique_ptr<ThreadPool> thread_pool_;
    CanRuntime* can_runtime_;
};
#endif
