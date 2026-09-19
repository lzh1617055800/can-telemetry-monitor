#include "CanBusStatistics.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const std::string& message)
{
    if(!condition)
    {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

CanFrame makeFrame(
    std::uint32_t id,
    bool is_extended,
    std::uint8_t dlc,
    std::uint64_t timestamp_ns)
{
    CanFrame frame{};
    frame.id = id;
    frame.is_extended = is_extended;
    frame.dlc = dlc;
    frame.timestamp_ns = timestamp_ns;
    return frame;
}
}

int main()
{
    bool rejected_invalid_bitrate = false;
    try
    {
        CanBusStatistics invalid(0);
    }
    catch(const std::invalid_argument&)
    {
        rejected_invalid_bitrate = true;
    }

    require(
        rejected_invalid_bitrate,
        "zero nominal bitrate should be rejected");

    CanBusStatistics statistics(500000, 1'000'000'000ULL);

    CanFrame standard = makeFrame(0x123, false, 4, 2'000'000'000ULL);
    standard.data[0] = 0x11;
    standard.data[1] = 0x22;
    statistics.recordReceived(standard);

    CanFrame extended = makeFrame(
        0x1ABCDE,
        true,
        2,
        2'000'000'100ULL);
    statistics.recordReceived(extended);

    const CanBusStatisticsSnapshot snapshot =
        statistics.snapshot(2'000'000'200ULL);

    require(
        snapshot.total_received_frames == 2,
        "statistics should count both received frames");
    require(
        snapshot.total_received_payload_bytes == 6,
        "statistics should count payload bytes");
    require(
        snapshot.receive_rate_fps == 2.0,
        "one-second window should report two frames per second");
    require(
        snapshot.estimated_bus_load_percent > 0.0,
        "received frames should produce non-zero bus load");
    require(
        snapshot.per_id.size() == 2,
        "statistics should keep separate standard and extended IDs");

    const CanBusStatisticsSnapshot expired =
        statistics.snapshot(3'100'000'200ULL);

    require(
        expired.receive_rate_fps == 0.0,
        "samples outside the window should expire");

    std::cout << "can_statistics_test: PASS\n";
    return EXIT_SUCCESS;
}
