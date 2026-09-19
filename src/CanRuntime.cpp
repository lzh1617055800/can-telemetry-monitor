#include "CanRuntime.h"

#include <utility>

CanRuntime::CanRuntime(
    std::string interface_name,
    std::uint32_t nominal_bitrate,
    std::size_t frame_capacity)
    : interface_name_(std::move(interface_name)),
      store_(frame_capacity),
      statistics_(nominal_bitrate)
{
}

CanRuntime::~CanRuntime()
{
    stop();
}

bool CanRuntime::start(std::string* error)
{
    if(running_)
    {
        if(error != nullptr)
        {
            *error = "CAN runtime is already running";
        }
        return false;
    }

    if(!sender_.open(interface_name_, error))
    {
        return false;
    }

    receiver_ = std::make_unique<CanReceiver>(
        interface_name_,
        store_,
        std::vector<CanIdFilter>{},
        &statistics_);

    if(!receiver_->start(error))
    {
        receiver_.reset();
        sender_.close();
        return false;
    }

    running_ = true;
    return true;
}

void CanRuntime::stop()
{
    if(receiver_ != nullptr)
    {
        receiver_->stop();
        receiver_.reset();
    }

    sender_.close();
    running_ = false;
}

bool CanRuntime::isRunning() const
{
    return running_;
}

std::vector<CanFrame> CanRuntime::queryFrames(
    std::uint64_t after_sequence,
    std::optional<std::uint32_t> id_filter,
    std::size_t limit) const
{
    return store_.queryAfter(after_sequence, id_filter, limit);
}

FrameStoreStats CanRuntime::frameStats() const
{
    return store_.stats();
}

CanBusStatisticsSnapshot CanRuntime::busStats() const
{
    return statistics_.snapshot(
        CanBusStatistics::monotonicNowNs());
}

bool CanRuntime::sendFrame(
    std::uint32_t id,
    bool is_extended,
    const std::vector<std::uint8_t>& data,
    std::string* error)
{
    const std::uint32_t maximum_id =
        is_extended ? 0x1FFFFFFFU : 0x7FFU;

    if(id > maximum_id)
    {
        if(error != nullptr)
        {
            *error = "CAN ID is out of range for the selected frame type";
        }
        return false;
    }

    if(data.size() > CanFrame::kMaxDataLength)
    {
        if(error != nullptr)
        {
            *error = "CAN data length must be between 0 and 8";
        }
        return false;
    }

    if(!sender_.isOpen())
    {
        if(error != nullptr)
        {
            *error = "CAN sender is not open";
        }
        return false;
    }

    return sender_.sendRaw(
        id,
        is_extended,
        data.empty() ? nullptr : data.data(),
        static_cast<std::uint8_t>(data.size()),
        error);
}
