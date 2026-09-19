#include "CanBusStatistics.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>

CanBusStatistics::CanBusStatistics(
    std::uint32_t nominal_bitrate,
    std::uint64_t window_ns)
    : nominal_bitrate_(nominal_bitrate),
      window_ns_(window_ns)
{
    if(nominal_bitrate_ == 0)
    {
        throw std::invalid_argument(
            "CAN nominal bitrate must be greater than zero");
    }

    if(window_ns_ == 0)
    {
        throw std::invalid_argument(
            "statistics window must be greater than zero");
    }
}

void CanBusStatistics::recordReceived(const CanFrame& frame)
{
    const std::uint32_t estimated_bits =
        estimateClassicCanBits(frame);

    std::lock_guard<std::mutex> lock(mutex_);

    ++total_received_frames_;
    total_received_payload_bytes_ += frame.dlc;

    const auto key =
        std::make_pair(frame.is_extended, frame.id);

    CanIdStatistics& id_statistics = per_id_[key];
    id_statistics.id = frame.id;
    id_statistics.is_extended = frame.is_extended;
    ++id_statistics.frame_count;
    id_statistics.payload_bytes += frame.dlc;
    id_statistics.last_timestamp_ns = frame.timestamp_ns;

    window_samples_.push_back(
        WindowSample{frame.timestamp_ns, estimated_bits});
    window_estimated_bits_ += estimated_bits;

    removeExpiredSamplesLocked(frame.timestamp_ns);
}

CanBusStatisticsSnapshot CanBusStatistics::snapshot(
    std::uint64_t now_ns) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    removeExpiredSamplesLocked(now_ns);

    CanBusStatisticsSnapshot result;
    result.total_received_frames = total_received_frames_;
    result.total_received_payload_bytes =
        total_received_payload_bytes_;

    const double window_seconds =
        static_cast<double>(window_ns_) / 1'000'000'000.0;

    result.receive_rate_fps =
        static_cast<double>(window_samples_.size()) /
        window_seconds;

    result.estimated_bus_load_percent =
        100.0 * static_cast<double>(window_estimated_bits_) /
        (static_cast<double>(nominal_bitrate_) * window_seconds);

    for(const auto& item : per_id_)
    {
        result.per_id.push_back(item.second);
    }

    return result;
}

std::uint64_t CanBusStatistics::monotonicNowNs()
{
    struct timespec timestamp{};
    ::clock_gettime(CLOCK_MONOTONIC, &timestamp);

    return static_cast<std::uint64_t>(timestamp.tv_sec) *
               1'000'000'000ULL +
           static_cast<std::uint64_t>(timestamp.tv_nsec);
}

std::uint32_t CanBusStatistics::estimateClassicCanBits(
    const CanFrame& frame)
{
    const std::uint32_t data_length =
        std::min(static_cast<std::uint32_t>(frame.dlc),
                 static_cast<std::uint32_t>(
                     CanFrame::kMaxDataLength));

    const std::uint32_t unstuffed_bits =
        (frame.is_extended ? 67U : 47U) + data_length * 8U;

    return unstuffed_bits + (unstuffed_bits + 4U) / 5U;
}

void CanBusStatistics::removeExpiredSamplesLocked(
    std::uint64_t now_ns) const
{
    while(!window_samples_.empty())
    {
        const WindowSample& oldest = window_samples_.front();

        if(now_ns < oldest.timestamp_ns ||
           now_ns - oldest.timestamp_ns < window_ns_)
        {
            break;
        }

        window_estimated_bits_ -= oldest.estimated_bits;
        window_samples_.pop_front();
    }
}
