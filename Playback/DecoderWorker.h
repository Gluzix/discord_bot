#pragma once

#include "PcmBuffer.h"
#include "PcmResampler.h"
#include "Song.h"

#include <functional>
#include <string>
#include <thread>

namespace dpp {
struct message;
}

struct ResolvedMedia;

// Resolves, announces and decodes one song into the buffer's producer side.
// =======================================================
// Rules:
// - Every exit from run() ends in buffer.markFinished(): the sender waits on
//   the queue and must be released.
// - A stop is checked in run() before yt-dlp, after the resolve and before
//   the announcement: a song stopped early never launches yt-dlp, a cancelled
//   resolve (empty too) is no error, and a stop during "Looking for your
//   song..." never announces.
// - The seek requester is set in decode() only after resampler.open(), the
//   duration first (it clamps a seek), and cleared before the resampler dies.
// - decode() outlives end of stream, waiting for a rewind in
//   buffer.finishedAndWaitForRewind(): the decoder runs a minute ahead, so
//   otherwise the last minute of every song couldn't be rewound.
// - songFailed is written by fail() on the thread and read by failed() after
//   join(): the join is the synchronisation point, so no lock is needed.
// =======================================================
class DecoderWorker
{
public:
    // Starts the thread. onLabelResolved is called (from it) with the rendered
    // "now playing" label once the song resolves.
    DecoderWorker(PcmBuffer &buffer_, Song &song_, std::function<void(std::string)> onLabelResolved_);
    ~DecoderWorker();

    DecoderWorker(const DecoderWorker &) = delete;
    DecoderWorker &operator=(const DecoderWorker &) = delete;

    void join();

    // Nothing was played.
    bool failed() const;

private:
    void run();
    void notifyUser(dpp::message msg);
    void fail(const char *msg);
    ResolvedMedia mediaToPlay();
    void decode();

    PcmResampler resampler;
    PcmBuffer &buffer;
    Song &song;
    std::function<void(std::string)> onLabelResolved;

    bool songFailed = false;

    std::thread thread;

};
