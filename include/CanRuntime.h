#ifndef CAN_RUNTIME_H
#define CAN_RUNTIME_H

#include "CanBusStatistics.h"
#include "CanReceiver.h"
#include "CanSender.h"
#include "FrameStore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class CanRuntime
{
public:
    explicit CanRuntime(
        std::string interface_name = "vcan0",
        std::uint32_t nominal_bitrate = 500000,
        std::size_t frame_capacity = 4096);

    ~CanRuntime();

    bool start(std::string* error);
    void stop();

    bool isRunning() const;

    std::vector<CanFrame> queryFrames(
        std::uint64_t after_sequence,
        std::optional<std::uint32_t> id_filter,
        std::size_t limit) const;

    FrameStoreStats frameStats() const;
    CanBusStatisticsSnapshot busStats() const;

    bool sendFrame(
        std::uint32_t id,
        bool is_extended,
        const std::vector<std::uint8_t>& data,
        std::string* error);

private:
    std::string interface_name_;
    FrameStore store_;
    CanBusStatistics statistics_;
    CanSender sender_;
    std::unique_ptr<CanReceiver> receiver_;
    bool running_{false};
};

#endif
