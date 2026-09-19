#include "FrameStore.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition,const std::string& message)
{
    if(!condition)
    {
        std::cerr<<"[FAIL]"<<message<<std::endl;
        std::exit(EXIT_FAILURE);
    }
}
}

CanFrame makeFrame(std::uint32_t id)
{
    CanFrame frame;
    frame.id = id;
    frame.dlc = 2;
    frame.data[0] = 0x12;
    frame.data[1] = 0x34;
    return frame;
}

int main()
{
    FrameStore store(2);

    require(
        store.append(makeFrame(0x100)) == 1,
        "first appended frame should receive sequence 1");

    require(
        store.append(makeFrame(0x200)) == 2,
        "second appended frame should receive sequence 2");

    require(
        store.append(makeFrame(0x300)) == 3,
        "third appended frame should receive sequence 3");

    const FrameStoreStats current_stats = store.stats();

    require(
        current_stats.total_appended == 3,
        "three frames should have been appended");

    require(
        current_stats.overwritten == 1,
        "one oldest frame should have been overwritten");

    require(
        current_stats.buffered == 2,
        "buffer should keep only two frames");

    const auto frames = store.queryAfter(0, std::nullopt, 10);

    require(
        frames.size() == 2,
        "query should return the two buffered frames");

    require(
        frames[0].sequence == 2 && frames[0].id == 0x200,
        "oldest remaining frame should be sequence 2 with ID 0x200");

    require(
        frames[1].sequence == 3 && frames[1].id == 0x300,
        "newest remaining frame should be sequence 3 with ID 0x300");


    const auto new_frames = store.queryAfter(2, std::nullopt, 10);

    require(
        new_frames.size() == 1,
        "queryAfter(2) should return only one newer frame");

    require(
        new_frames[0].sequence == 3 &&
        new_frames[0].id == 0x300,
        "queryAfter(2) should return sequence 3");

    const auto filtered_frames =
        store.queryAfter(
            0,
            std::optional<std::uint32_t>{0x200},
            10
        );

    require(
        filtered_frames.size() == 1,
        "ID filter should return one matching frame");

    require(
        filtered_frames[0].id == 0x200,
        "ID filter should return frame 0x200");

    const auto limited_frames =
        store.queryAfter(0, std::nullopt, 1);

    require(
        limited_frames.size() == 1,
        "limit 1 should return at most one frame");

    require(
        limited_frames[0].sequence == 2,
        "limited result should preserve queue order");

    bool rejected_zero_capacity = false;

    try
    {
        FrameStore invalid_store(0);
    }
    catch(const std::invalid_argument&)
    {
        rejected_zero_capacity = true;
    }

    require(
        rejected_zero_capacity,
        "zero capacity should throw invalid_argument");

    std::cout << "frame_store_test: PASS\n";
    return EXIT_SUCCESS;
}
