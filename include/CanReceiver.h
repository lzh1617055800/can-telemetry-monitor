#ifndef CAN_RECEIVER_H
#define CAN_RECEIVER_H
#include "CanIdFilter.h"
#include "CanBusStatistics.h"
#include "FrameStore.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CanReceiver
{
public:
    CanReceiver(
        std::string interface_name,
        FrameStore& store,
        std::vector<CanIdFilter> filters = {},
        CanBusStatistics* statistics = nullptr);
    ~CanReceiver();

    bool start(std::string* error);
    void stop();
private:
    bool openSocket(std::string* error);
    void receiveLoop();

    std::string interface_name_;
    FrameStore& store_;
    std::vector<CanIdFilter> filters_;
    CanBusStatistics* statistics_{nullptr};

    std::mutex lifecycle_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<int> socket_fd_{-1};
    std::thread receive_thread_;

};

#endif
