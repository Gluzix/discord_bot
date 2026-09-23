#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "Song.h"

// Resolves the next few queued songs ahead of time, on its own thread.
// =======================================================
// Rules:
// - The queue, cv and mutex are PlaybackController's: every queue access,
//   nextUnresolved() included, holds stateMutex.
// - yt-dlp runs with stateMutex dropped and the queue may change meanwhile,
//   so a result is written back by song id.
// - The "Queued at position N" edit goes out once stateMutex is dropped:
//   no dpp call under it.
// =======================================================
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
    std::thread thread;
};
