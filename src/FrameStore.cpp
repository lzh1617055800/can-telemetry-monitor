#include "FrameStore.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>

FrameStore::FrameStore(std::size_t capacity)
    :capacity_(capacity)
{
    if(capacity_ == 0)
    {
        throw std::invalid_argument(
            "容量至少大于0"
        );
    }
}

std::uint64_t FrameStore::append(CanFrame frame)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const std::uint64_t assigned_sequence = next_sequence_++;
    frame.sequence = assigned_sequence;
    ++total_appended_;

    if(frames_.size() >= capacity_)
    {
        frames_.pop_front();
        ++overwritten_;
    }
    frames_.push_back(std::move(frame));
    return assigned_sequence;
}

std::vector<CanFrame> FrameStore::queryAfter(
        std::uint64_t after_sequence,
        std::optional<std::uint32_t> id_filter,
        std::size_t limit)const
{
    std::vector<CanFrame> result;

    if(limit == 0)
    {
        return result;
    }
    std::shared_lock<std::shared_mutex> lock(mutex_);

    result.reserve(std::min(limit,frames_.size()));

    for(const auto& frame : frames_)
    {
        if(frame.sequence <= after_sequence)
        {
            continue;
        }
        if(id_filter.has_value() && frame.id != *id_filter)
        {
            continue;
        }

        result.push_back(frame);

        if(result.size() >= limit)
        {
            break;
        }
    }
    return result;
}

FrameStoreStats FrameStore::stats()const{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    return FrameStoreStats
    {
        total_appended_,
        overwritten_,
        frames_.size()
    };
}
