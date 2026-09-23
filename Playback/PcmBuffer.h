#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

// Everything a song's decoder and sender share; no dpp, FFmpeg or Qt.
// =======================================================
// Rules:
// - Only stop() sets isPlaying false, and it does so under queueMutex: the
//   flag is in the queueCv predicates, so no waiter can miss the wakeup.
// - reset() leaves isPlaying alone: arm() comes before play(), and a stop()
//   in between must still end the song.
// - The seek requester runs under queueMutex, so it must not take a lock the
//   decoder holds while it pushes: the order is queueMutex, then the
//   resampler's pendingSeekMutex.
// - From a seek the queue can't serve until seekApplied() brings the newest
//   ticket, seekInFlight holds and every push is Stale: the packets still in
//   the decoder are from before the jump.
// =======================================================
class PcmBuffer
{
public:
    void arm();
    void stop();
    bool running() const;

    void reset();

    enum class PushResult { Queued, Stale, Stopped };

    // Blocks while the queue is at its cap.
    PushResult push(std::vector<uint8_t> packet);

    void markFinished();

    // Blocks until a seek (true: decode again) or a stop (false).
    bool finishedAndWaitForRewind();

    void setDuration(double seconds);

    using SeekRequester = std::function<void(double seconds, uint64_t ticket)>;
    void setSeekRequester(SeekRequester requester);
    void clearSeekRequester();

    void seekApplied(uint64_t ticket, bool ok);

    // Leftover audio means the song was cut short (skip/stop).
    bool cutShort() const;

    struct Position
    {
        double seconds{0};
        double durationSeconds{0}; // 0 = unknown
    };

    // `seconds` is added to the current position when relative, otherwise
    // it is the target itself. nullopt when there is nothing to seek in
    // right now.
    std::optional<Position> seek(double seconds, bool relative);

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

private:
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
    size_t queuedBytes = 0;
    uint64_t playedBytes = 0;     // song offset of the next packet the sender pops
    uint64_t seekTicket = 0;
    bool seekInFlight = false;
    uint64_t bytesBeforeSeek = 0; // to restore the position if the seek fails
    size_t droppedForSeek = 0;
    SeekRequester seekRequester;  // set only while the decoder has a resampler open
    bool songEnding = false;      // the sender took the natural-end exit; too late to seek
    bool flushClient = false;     // asks the sender to drop dpp's send buffer
    double durationSeconds = 0;   // 0 = the container didn't say
    bool decodingFinished = false;
};
