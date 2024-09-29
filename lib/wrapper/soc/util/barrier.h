#pragma once
#include <mutex>
#include <condition_variable>

namespace nicc {
class ThreadBarrier {
public:
    explicit ThreadBarrier(int numThreads) : count(0), totalThreads(numThreads), generation(0) {}

    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        int gen = generation;

        if (++count < totalThreads) {
            condition.wait(lock, [this, gen]() { return gen != generation; });
        } else {
            count = 0;
            generation++;
            condition.notify_all();
        }
    }

private:
    int count;
    int totalThreads;
    int generation;
    std::mutex mutex;
    std::condition_variable condition;
};
}