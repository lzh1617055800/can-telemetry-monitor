#ifndef TIMER_h
#define TIMER_H
#include <queue>
#include <functional>
#include <ctime>
#include <unordered_map>
#include <vector>
#include <mutex>
using namespace std;
struct TimerEvent
{
    int fd;
    time_t expire_time;
    bool operator>(const TimerEvent& tet)const
    {
        return expire_time > tet.expire_time;
    }
};

class Timer
{
public:
    Timer();
    void addTimer(int fd,int timeout_sec);
    void updateTimer(int fd,int timeout_sec);
    void removeTimer(int fd);
    vector<int> tick();
private:
    priority_queue<TimerEvent,vector<TimerEvent>,greater<TimerEvent>> heap_;
    unordered_map<int, time_t> fd_expire_map_;
    mutable mutex mutex_;
};

#endif
