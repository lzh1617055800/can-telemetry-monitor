#include "CanReceiver.h"
#include "CanSender.h"
#include "CanBusStatistics.h"
#include "FrameStore.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

int main()
{
    FrameStore store(100);
    CanBusStatistics statistics(500000);

    const std::vector<CanIdFilter> filters{
        CanIdFilter{0x123,false}
    };
    CanReceiver receiver(
        "vcan0",
        store,
        filters,
        &statistics);

    std::string error;

    if(!receiver.start(&error))
    {
        std::cerr << "failed to start receiver: "
                  << error << '\n';
        return EXIT_FAILURE;
    }

    CanSender sender;

    if(!sender.open("vcan0", &error))
    {
        std::cerr << "failed to open sender: "
                  << error << '\n';

        receiver.stop();
        return EXIT_FAILURE;
    }

    const std::array<std::uint8_t, 4> payload{
        0x11,
        0x22,
        0x33,
        0x44
    };

    if(!sender.sendRaw(
           0x123,
           false,
           payload.data(),
           static_cast<std::uint8_t>(payload.size()),
           &error))
    {
        std::cerr << "failed to send CAN frame: "
                  << error << '\n';

        sender.close();
        receiver.stop();
        return EXIT_FAILURE;
    }

    if(!sender.sendRaw(0x456,false,payload.data(),static_cast<std::uint8_t>(payload.size()),&error))
    {
        std::cerr<<"failed to send blocked CAN frame: "
                <<error<<"\n";
        sender.close();
        receiver.stop();
        return EXIT_FAILURE;
    }

    std::vector<CanFrame> received_frames;

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(1);

    while(std::chrono::steady_clock::now() < deadline)
    {
        received_frames = store.queryAfter(
            0,
            std::optional<std::uint32_t>{0x123},
            1);

        if(!received_frames.empty())
        {
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto blocked_frames = store.queryAfter(0,std::optional<std::uint32_t>{0x456},1);
    if(!blocked_frames.empty())
    {
        std::cerr<<"CAN filter failed: ID 0X456 was received\n";
        sender.close();
        receiver.stop();
        return EXIT_FAILURE;
    }
    sender.close();
    receiver.stop();

    if(received_frames.empty())
    {
        std::cerr << "did not receive CAN ID 0x123\n";
        return EXIT_FAILURE;
    }

    const CanFrame& received = received_frames.front();

    const bool is_expected_frame =
        received.id == 0x123 &&
        !received.is_extended &&
        received.direction == CanFrameDirection::Rx &&
        received.dlc == payload.size() &&
        std::equal(
            payload.begin(),
            payload.end(),
            received.data.begin());

    if(!is_expected_frame)
    {
        std::cerr << "received CAN frame does not match\n";
        return EXIT_FAILURE;
    }

    const CanBusStatisticsSnapshot statistics_snapshot =
        statistics.snapshot(
            CanBusStatistics::monotonicNowNs());

    if(statistics_snapshot.total_received_frames != 1)
    {
        std::cerr << "statistics count mismatch: "
                  << statistics_snapshot.total_received_frames
                  << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "CAN LOOPBACK PASS: accepted ID=0x123, "
              << "rejected ID=0x456, received="
              << statistics_snapshot.total_received_frames
              << ", load="
              << statistics_snapshot.estimated_bus_load_percent
              << "%\n";

    return EXIT_SUCCESS;
}
