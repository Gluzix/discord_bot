#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

class PcmBuffer
{
public:
    PcmBuffer() = default;

    void arm();
    void stop();
    bool running() const;

    // TODO: To Be removed in future refactor
    std::mutex &mutex();
    // TODO: To Be removed in future refactor
    std::condition_variable &cv();

private:
    std::atomic<bool> isPlaying{false};
    std::mutex queueMutex;
    std::condition_variable queueCv;
};
