#ifndef CANSENDER_H
#define CANSENDER_H
#include "CanFrame.h"
#include <atomic>
#include <cstdint>
#include <string>

class CanSender
{
public:
    CanSender() = default;
    ~CanSender();

    bool open(const std::string& interface_name,std::string* error);
    bool send(const CanFrame& frame,std::string* error);
    bool sendRaw(uint32_t id,bool is_extended,const uint8_t* data,uint8_t dlc,std::string* error);

    void close();
    bool isOpen() const;
private:
    bool openSocket(const std::string& interface_name,std::string* error);
    std::atomic<int> socket_fd_{-1};
};
#endif