#ifndef FRAMESTORE_H
#define FRAMESTORE_H
#include "CanFrame.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <shared_mutex>
#include <vector>

struct FrameStoreStats
{
    std::uint64_t total_appended{0};//总共存了多少条报文
    std::uint64_t overwritten{0};//缓存满了之后覆盖旧报文的次数
    std::size_t buffered{0};//当前缓存里面还存着多少帧
};

class FrameStore
{
public:
    explicit FrameStore(std::size_t capacity);//容量即最多存储多少条报文


    std::uint64_t append(CanFrame frame);//把一条CanFrame放进缓存，返回这条报文的序列号,缓存要是满了就删掉最旧的数据，然后overwritten+1
    std::vector<CanFrame> queryAfter(
        std::uint64_t after_sequence,//只查询序列号大于该数值的报文，用于增量拉取
        std::optional<std::uint32_t> id_filter,//过滤CAN ID optional可以不填写数值代表不过滤
        std::size_t limit//最多返回多少条结果，防止一次加载过多
    ) const;

    FrameStoreStats stats() const;//获取缓存数据

private:
    std::size_t capacity_;//缓存最大的容量
    std::uint64_t next_sequence_{1};//下一条报文要分配的序列号
    std::uint64_t total_appended_{0};//存入的报文总数
    std::uint64_t overwritten_{0};//覆盖旧帧的次数

    std::deque<CanFrame> frames_;//存放CAN帧的队列
    mutable std::shared_mutex mutex_;//读写锁保证多线程安全

};
#endif