#ifndef CAN_BUS_STATISTICS_H
#define CAN_BUS_STATISTICS_H
#include "CanFrame.h"
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

struct CanIdStatistics
{
    std::uint32_t id{0};
    bool is_extended{false};
    std::uint64_t frame_count{0};
    std::uint64_t payload_bytes{0};
    std::uint64_t last_timestamp_ns{0};
};

struct CanBusStatisticsSnapshot
{
    std::uint64_t total_received_frames{0};
    std::uint64_t total_received_payload_bytes{0};

    double receive_rate_fps{0.0};
    double estimated_bus_load_percent{0.0};

    std::vector<CanIdStatistics> per_id;
};

class CanBusStatistics
{
public:
    explicit CanBusStatistics(std::uint32_t nominal_bitrate,std::uint64_t window_ns = 1'000'000'000ULL);
    void recordReceived(const CanFrame& frame);
    CanBusStatisticsSnapshot snapshot(std::uint64_t now_ns) const;
    static std::uint64_t monotonicNowNs();

private:
    struct WindowSample
    {
        std::uint64_t timestamp_ns{0};
        std::uint32_t estimated_bits{0};
    };

    static std::uint32_t estimateClassicCanBits(const CanFrame& frame);
    void removeExpiredSamplesLocked(std::uint64_t now_ns) const;

    std::uint32_t nominal_bitrate_;
    std::uint64_t window_ns_;

    mutable std::mutex mutex_;

    std::uint64_t total_received_frames_{0};
    std::uint64_t total_received_payload_bytes_{0};

    std::map<std::pair<bool,std::uint32_t>,CanIdStatistics> per_id_;

    mutable std::deque<WindowSample> window_samples_;
    mutable std::uint64_t window_estimated_bits_{0};
};
#endif
