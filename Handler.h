#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>

class Handler
{
public:
    Handler(){}
    void resample();

private:

    std::atomic<bool> isPlaying{false};

    std::thread audioThread;
    std::thread resamplingThread;


    std::vector<uint8_t> pcmData{};
    std::queue<std::vector<uint8_t>> audioQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;
};
