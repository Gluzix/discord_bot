#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "Song.h"

class ResolverWorker
{
public:
    ResolverWorker(std::deque<Song> &songQueue_,
                   std::condition_variable &stateCv_,
                   std::mutex &stateMutex_);
    ~ResolverWorker();

private:
    void run();
    Song *nextUnresolved();
    void resolveSongs(const uint64_t songId, const std::string &target);

    std::atomic<bool> running{true};
    std::deque<Song> &songQueue;
    std::condition_variable &stateCv;
    std::mutex &stateMutex;
    std::thread thread; // started in the ctor, stopped and joined in the dtor
};
