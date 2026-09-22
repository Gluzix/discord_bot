#pragma once

#include "PcmBuffer.h"

#include <thread>

namespace dpp {
class discord_voice_client;
}

// Paces one song's PCM packets into the voice client: the only thread that
// may touch the client. Every exit ends in buffer.stop().
class SenderWorker
{
public:
    // Starts the thread.
    SenderWorker(PcmBuffer &buffer_, dpp::discord_voice_client *voiceClient_);
    ~SenderWorker();

    SenderWorker(const SenderWorker &) = delete;
    SenderWorker &operator=(const SenderWorker &) = delete;

    void join();

    // The client stopped taking audio; it must not be used again.
    bool voiceLost() const;

private:
    void run();

    PcmBuffer &buffer;
    dpp::discord_voice_client *voiceClient;

    // Written by the thread, read after it joins - the join is the
    // synchronisation point, so no lock is needed.
    bool lost = false;

    std::thread thread; // started in the ctor, joined by join() or the dtor
};
