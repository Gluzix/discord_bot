#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <vector>

class PcmBuffer
{
public:
    PcmBuffer() = default;

    void arm();
    void stop();
    bool running() const;

    // Per song; leaves the live flag alone - arm() comes before play().
    void reset();

    enum class PushResult { Queued, Stale, Stopped };

    // Blocks while the queue is at its cap.
    PushResult push(std::vector<uint8_t> packet);

    void markFinished();

    // Blocks until a seek (true: decode again) or a stop (false).
    bool finishedAndWaitForRewind();

    void setDuration(double seconds);

    // Leftover audio means the song was cut short (skip/stop).
    bool cutShort() const;

    struct Next
    {
        enum class Kind { Packet, Flush, Ended, Stopped } kind;
        std::vector<uint8_t> packet; // only for Kind::Packet
    };

    // Blocks until there is something for the sender to do.
    Next next();

    // A seek asks for dpp's ~1s tail to go, so the jump is audible at once.
    bool takeFlushRequest();

    // Returns early on a stop or a flush request.
    void pacingWait(std::chrono::milliseconds timeout);

    // TODO: To Be removed in future refactor
    std::mutex &mutex();
    // TODO: To Be removed in future refactor
    std::condition_variable &cv();

private:
    friend class SongPlayer; // TODO: scaffolding until the state is behind methods

    std::atomic<bool> isPlaying{false};
    mutable std::mutex queueMutex;
    std::condition_variable queueCv;

    // The decoder runs at most MAX_QUEUED_SECONDS ahead of playback - a cap
    // on memory (a whole album as PCM is hundreds of MB), not a pacing device.
    static constexpr size_t BYTES_PER_SECOND = 192000; // 48kHz * 2ch * 2 bytes
    static constexpr size_t MAX_QUEUED_SECONDS = 60;
    static constexpr size_t MAX_QUEUED_BYTES = MAX_QUEUED_SECONDS * BYTES_PER_SECOND;

    // Everything down to decodingFinished is guarded by queueMutex.
    std::queue<std::vector<uint8_t>> audioQueue;
    size_t queuedBytes = 0;       // what audioQueue holds
    uint64_t playedBytes = 0;     // song offset of the next packet the sender pops
    uint64_t seekTicket = 0;      // incremented per real seek; the latest one wins
    bool seekInFlight = false;    // the decoder hasn't applied it yet: its packets are stale
    uint64_t bytesBeforeSeek = 0; // to restore the position if the seek fails
    size_t droppedForSeek = 0;
    bool songEnding = false;      // the sender took the natural-end exit; too late to seek
    bool flushClient = false;     // asks the sender to drop dpp's send buffer
    double durationSeconds = 0;   // 0 = the container didn't say
    bool decodingFinished = false;
};
