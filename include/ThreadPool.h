#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
using namespace std;
class ThreadPool
{
public:
    explicit ThreadPool(int num_threads);
    ~ThreadPool();
    void submit(function<void()> task);

private:
    void workerLoop();

    vector<thread> threads_;
    queue<function<void()>> task_queue_;
    mutex mtx_;
    condition_variable cv_;
    bool stop_;
};
#endif