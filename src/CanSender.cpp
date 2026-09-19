#include "CanSender.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

CanSender::~CanSender()
{
    close();
}

bool CanSender::open(const std::string& interface_name,std::string* error)
{
    if(socket_fd_.load(std::memory_order_acquire) >= 0)
    {
        if(error != nullptr)
        {
            *error = "CAN sender is already open";
        }

        return false;
    }

    return openSocket(interface_name, error);
}

bool CanSender::openSocket(const std::string& interface_name,std::string* error)
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
    std::strncpy(interface_request.ifr_name,interface_name.c_str(),IFNAMSIZ - 1);
    interface_request.ifr_name[IFNAMSIZ - 1] = '\0';
    if(::ioctl(fd,SIOCGIFINDEX,&interface_request) < 0)
    {
        const int error_number = errno;
        if(error != nullptr)
        {
            *error = "cannot find CAN interface '";
            *error += interface_name;
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
            *error += interface_name;
            *error += "': ";
            *error += std::strerror(error_number);
        }
        ::close(fd);
        return false;
    }

    socket_fd_.store(fd, std::memory_order_release);
    return true;
}

bool CanSender::send(const CanFrame& frame,std::string* error)
{
    return sendRaw(frame.id,frame.is_extended,frame.data.data(),frame.dlc,error);
}

bool CanSender::sendRaw(std::uint32_t id,bool is_extended,const std::uint8_t* data,std::uint8_t dlc,std::string* error)
{
    const int fd =socket_fd_.load(std::memory_order_acquire);
    if(fd < 0)
    {
        if(error != nullptr)
        {
            *error = "CAN sender is not open";
        }
        return false;
    }
    if(dlc > CanFrame::kMaxDataLength)
    {
        if(error != nullptr)
        {
            *error = "CAN data length must be between 0 and 8";
        }
        return false;
    }
    if(dlc > 0 && data == nullptr)
    {
        if(error != nullptr)
        {
            *error = "data pointer cannot be null when dlc is not zero";
        }
        return false;
    }
    struct can_frame kernel_frame{};

    if(is_extended)
    {
        kernel_frame.can_id =CAN_EFF_FLAG | (id & CAN_EFF_MASK);
    }
    else
    {
        kernel_frame.can_id = id & CAN_SFF_MASK;
    }

    kernel_frame.can_dlc = dlc;
    if(dlc > 0)
    {
        std::copy_n(
            data,
            dlc,
            kernel_frame.data);
    }

    const ssize_t written = ::write(
        fd,
        &kernel_frame,
        CAN_MTU);

    if(written < 0)
    {
        const int error_number = errno;

        if(error != nullptr)
        {
            *error = "cannot send CAN frame: ";
            *error += std::strerror(error_number);
        }

        return false;
    }

    if(written != CAN_MTU)
    {
        if(error != nullptr)
        {
            *error = "CAN frame was only partially written";
        }

        return false;
    }

    return true;
}

void CanSender::close()
{
    const int fd = socket_fd_.exchange(-1,std::memory_order_acq_rel);
    if(fd >= 0)
    {
        ::close(fd);
    }
}

bool CanSender::isOpen() const
{
    return socket_fd_.load(
        std::memory_order_acquire) >= 0;
}