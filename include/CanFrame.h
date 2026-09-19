#ifndef CANFRAME_H
#define CANFRAME_H
#include <array>
#include <cstddef>
#include <cstdint>
enum class CanFrameDirection : std::uint8_t
{
    Rx,
    Tx
};

struct CanFrame{
    static constexpr std::size_t kMaxDataLength = 8;
    std::uint64_t sequence{0};//序列号用来区分先后顺序
    std::uint64_t timestamp_ns{0};//时间戳，用于做时序分析
    std::uint32_t id{0};//每条CAN报文唯一标识，用来区分信号

    std::array<std::uint8_t,kMaxDataLength> data{};//安全，带长度，有边界检查
    std::uint8_t dlc{0};//真是有效的数据长度

    bool is_extended{false};//是否是扩展的 CAN ID
    CanFrameDirection direction{CanFrameDirection::Rx};//报文收发的方向，默认是接受
};
#endif