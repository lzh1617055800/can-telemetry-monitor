#ifndef CAN_ID_FILTER_H
#define CAN_ID_FILTER_H

#include <cstdint>

struct CanIdFilter
{
    std::uint32_t id{0};
    bool is_extended{false};
};

#endif