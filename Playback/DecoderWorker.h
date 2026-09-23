#pragma once

#include "Song.h"
#include "PcmBuffer.h"
#include "PcmResampler.h"
#include "WindowsProcessRunner.h"

#include <functional>
#include <string>
#include <thread>

namespace dpp {
struct message;
}

// Resolves, announces and decodes one song into the buffer's producer side.
class DecoderWorker
{
public:
    // Starts the thread. onLabelResolved is called (from it) with the rendered
    // "now playing" label once the song resolves, so /queue can show the title.
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
    ResolvedMedia computeResolveMedia();
    void innerRun();

    PcmBuffer &buffer;
    Song &song;
    std::function<void(std::string)> onLabelResolved;

    // Written by the thread, read after it joins - the join is the
    // synchronisation point, so no lock is needed.
    bool songFailed = false;

    std::thread thread; // started in the ctor, joined by join() or the dtor

    PcmResampler resampler;
};
