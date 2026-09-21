#pragma once

#include <atomic>
#include <mutex>

class PcmBuffer
{
public:
    PcmBuffer();

    void arm();
    void stop();
    bool running();

    std::mutex &mutex();
    std::condition_variable &cv();

private:
    std::atomic<bool> isPlaying{false};
    std::mutex queueMutex{};
    std::condition_variable queueCv{};
};
