#include "CanReceiver.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <system_error>
#include <utility>
#include <poll.h>
#include <vector>

CanReceiver::CanReceiver(
    std::string interface_name,
    FrameStore& store,
    std::vector<CanIdFilter> filters,
    CanBusStatistics* statistics)
    : interface_name_(std::move(interface_name)),
      store_(store),
      filters_(std::move(filters)),
      statistics_(statistics)
{
}

CanReceiver::~CanReceiver()
{
    stop();
}

void CanReceiver::receiveLoop()
{
    while(running_.load(std::memory_order_acquire))
    {
        const int fd =socket_fd_.load(std::memory_order_acquire);
        if(fd < 0)
        {
            break;
        }
        struct pollfd poll_descriptor{};
        poll_descriptor.fd = fd;
        poll_descriptor.events = POLLIN;
        const int poll_result = ::poll(&poll_descriptor,1,100);
        if(poll_result < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            break;
        }
        if(poll_result == 0)
        {
            continue;
        }
        if((poll_descriptor.revents &(POLLERR | POLLHUP | POLLNVAL)) != 0)
        {
            break;
        }
        if((poll_descriptor.revents & POLLIN) == 0)
        {
            continue;
        }
        struct can_frame kernel_frame{};
        const ssize_t received_bytes = ::read(fd,&kernel_frame,CAN_MTU);
        if(received_bytes < 0)
        {
            if(errno == EINTR || errno == EAGAIN)
            {
                continue;
            }
            break;
        }
        if(received_bytes != CAN_MTU)
        {
            continue;
        }
        if((kernel_frame.can_id &(CAN_ERR_FLAG | CAN_RTR_FLAG)) != 0)
        {
            continue;
        }
        if(kernel_frame.can_dlc >CanFrame::kMaxDataLength)
        {
            continue;
        }
        struct timespec timestamp{};
        ::clock_gettime(
            CLOCK_MONOTONIC,
            &timestamp);
        CanFrame frame{};
        frame.timestamp_ns =
            static_cast<std::uint64_t>(timestamp.tv_sec) *
                1'000'000'000ULL +
            static_cast<std::uint64_t>(timestamp.tv_nsec);
        frame.id = kernel_frame.can_id &
            ((kernel_frame.can_id & CAN_EFF_FLAG) != 0
                ? CAN_EFF_MASK
                : CAN_SFF_MASK);
        frame.is_extended =
            (kernel_frame.can_id & CAN_EFF_FLAG) != 0;
        frame.dlc = kernel_frame.can_dlc;
        frame.direction = CanFrameDirection::Rx;
        std::copy_n(
            kernel_frame.data,
            frame.dlc,
            frame.data.begin());
        if(statistics_ != nullptr)
        {
            statistics_->recordReceived(frame);
        }

        store_.append(std::move(frame));
    }
}

bool CanReceiver::start(std::string* error)
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if(running_.load(std::memory_order_acquire))
    {
        if(error)
        {
            *error = "CanReceiver 已经启动";
        }
        return false;
    }

    if(!openSocket(error))
    {
        return false;
    }
    running_.store(true,std::memory_order_release);
    try{receive_thread_ = std::thread(&CanReceiver::receiveLoop,this);}
    catch(const std::system_error& exception)
    {
        running_.store(false,std::memory_order_release);
        const int fd = socket_fd_.exchange(-1,std::memory_order_acq_rel);
        if(fd >= 0)

        {
            ::close(fd);
        }
        if(error != nullptr)
        {
            *error = "cannot start can receive thread";
            *error += exception.what();
        }
        return false;
    }
    return true;
}

void CanReceiver::stop()
{
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    running_.store(false,std::memory_order_release);

    if(receive_thread_.joinable())
    {
        receive_thread_.join();
    }
    const int fd = socket_fd_.exchange(-1,std::memory_order_acq_rel);
    if(fd >= 0)
    {
        ::close(fd);
    }
}

bool CanReceiver::openSocket(std::string* error)
{
    const int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);

    if(fd < 0)
    {
        const int error_number = errno;

        if(error != nullptr)
        {
            *error = "cannot create CAN socket: ";
            *error += std::strerror(error_number);
        }

        return false;
    }

    struct ifreq interface_request{};

    std::strncpy(interface_request.ifr_name,interface_name_.c_str(),IFNAMSIZ - 1);
    interface_request.ifr_name[IFNAMSIZ - 1] = '\0';

    if(::ioctl(fd,SIOCGIFINDEX,&interface_request) < 0)
    {
        const int error_number = errno;
        if(error != nullptr)
        {
            *error = "cannot find CAN interface '";
            *error += interface_name_;
            *error += "': ";
            *error += std::strerror(error_number);
        }

        ::close(fd);
        return false;
    }

    struct sockaddr_can address{};

    address.can_family = AF_CAN;
    address.can_ifindex = interface_request.ifr_ifindex;

    if(::bind(fd,reinterpret_cast<struct sockaddr*>(&address),sizeof(address)) < 0)
    {
        const int error_number = errno;

        if(error != nullptr)
        {
            *error = "cannot bind CAN interface '";
            *error += interface_name_;
            *error += "': ";
            *error += std::strerror(error_number);
        }

        ::close(fd);
        return false;
    }
    if(!filters_.empty())
    {
        std::vector<struct can_filter> kernel_filters;
        kernel_filters.reserve(filters_.size());
        for(const CanIdFilter& filter : filters_)
        {
            const std::uint32_t max_id = filter.is_extended ? CAN_EFF_MASK : CAN_SFF_MASK;
            if((filter.id & ~max_id) != 0)
            {
                if(error != nullptr)
                {
                    *error = "CAN filter ID is out of range";
                }
                ::close(fd);
                return false;
            }
            struct can_filter kernel_filter{};
            if(filter.is_extended)
            {
                kernel_filter.can_id =
                    CAN_EFF_FLAG | filter.id;
                kernel_filter.can_mask = CAN_EFF_FLAG | CAN_EFF_MASK;
            }
            else{
                kernel_filter.can_id = filter.id;
                kernel_filter.can_mask = CAN_EFF_FLAG | CAN_SFF_MASK;
            }
            kernel_filters.push_back(kernel_filter);
        }

        const socklen_t filter_bytes = static_cast<socklen_t>(kernel_filters.size()) * sizeof(struct can_filter);
        if(::setsockopt(fd,SOL_CAN_RAW,CAN_RAW_FILTER,kernel_filters.data(),filter_bytes) < 0)
        {
            const int error_number = errno;
            if(error != nullptr)
            {
                *error = "cannot install CAN filters";
                *error += std::strerror(error_number);
            }
            ::close(fd);
            return false;
        }
    }
    socket_fd_.store(fd, std::memory_order_release);

    return true;
}
