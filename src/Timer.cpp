#include "Timer.h"
#include <iostream>
#include <chrono>
using namespace std;

Timer::Timer(){}

void Timer::addTimer(int fd, int timeout_sec)
{
    lock_guard<mutex> lock(mutex_);
    time_t now = time(nullptr);
    time_t expire = now + timeout_sec;
    heap_.push({fd,expire});
    fd_expire_map_[fd] = expire;
}

void Timer::updateTimer(int fd,int timeout_sec)
{
    lock_guard<mutex> lock(mutex_);
    time_t now = time(nullptr);
    time_t expire = now + timeout_sec;
    heap_.push({fd,expire});
    fd_expire_map_[fd] = expire;
}

void Timer::removeTimer(int fd)
{
    lock_guard<mutex> lock(mutex_);
    fd_expire_map_.erase(fd);
}

vector<int> Timer::tick()
{
    lock_guard<mutex> lock(mutex_);
    vector<int> expired_fd;
    time_t now = time(nullptr);
    while(!heap_.empty() && heap_.top().expire_time <= now)
    {
        TimerEvent event = heap_.top();
        heap_.pop();
        auto it = fd_expire_map_.find(event.fd);
        if(it != fd_expire_map_.end() && it->second == event.expire_time)
        {
            expired_fd.push_back(event.fd);
            fd_expire_map_.erase(event.fd);
        }
    }
    return expired_fd;
}
