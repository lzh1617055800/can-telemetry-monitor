#ifndef CONNECTION_H
#define CONNECTION_H
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include "EventLoop.h"
#include "HttpRequest.h"
class EventLoop;
class CanRuntime;
using namespace std;
class Connection
{
public:
    Connection(
        int fd,
        EventLoop* loop,
        CanRuntime* can_runtime = nullptr);
    ~Connection();

    void handleRead();
    void handleWrite();
    int getFd() const{return fd_;}
    void appendSendBuf(const string& data);
    bool isSse() const
    {
        return sse_active_.load(std::memory_order_acquire);
    }
    bool pumpSse();
    bool sseOverflowed() const
    {
        return sse_overflow_.load(std::memory_order_acquire);
    }
private:
    void processRequest();
    void startSse(std::uint64_t after_sequence);
    bool appendSsePayload(const std::string& payload);

    static constexpr std::size_t kMaxSsePendingBytes = 256 * 1024;

    int fd_;
    EventLoop* loop;
    string recv_buf;
    string send_buf;
    std::mutex read_mutex_;
    mutable std::mutex send_mutex_;
    std::mutex write_mutex_;
    std::mutex sse_mutex_;
    bool is_closed; //标记连接是否已经关闭
    HttpRequest req;
    CanRuntime* can_runtime_;
    std::atomic_bool sse_active_{false};
    std::atomic_bool sse_overflow_{false};
    std::uint64_t sse_after_sequence_{0};
    std::chrono::steady_clock::time_point sse_last_heartbeat_{};
};
#endif
