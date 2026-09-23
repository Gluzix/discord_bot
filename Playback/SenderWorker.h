#pragma once

#include "PcmBuffer.h"
#include "VoiceDrainWatchdog.h"

#include <thread>

namespace dpp {
class discord_voice_client;
}


// Paces one song's PCM packets into the voice client.
// =======================================================
// Rules:
// - Of the song's threads only this one, run(), touches the voice client;
//   SongPlayer::play() touches it only after join().
// - run(), flushClientNow() and waitForClientToDrain() check terminating
//   before every client call: dpp sets it at least 100 ms before it deletes
//   the client.
// - A dropped voice session leaves dpp retrying forever with a send buffer
//   that never drains again; the watchdog in waitForClientToDrain() turns
//   that into voiceLost().
// - No dpp call while the buffer's mutex is held, in any method - a foreign
//   lock inside our critical section wedged the whole pipeline once.
// - Every exit from run() ends in buffer.stop(): the decoder waits inside the
//   buffer and nobody else wakes it.
// - lost is written by run(), flushClientNow() and waitForClientToDrain(),
//   and read by voiceLost() after join(): the join is the synchronisation
//   point, so no lock is needed.
// =======================================================
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
    bool flushClientNow();
    void waitForClientToDrain();

    VoiceDrainWatchdog watchdog;

    PcmBuffer &buffer;
    dpp::discord_voice_client *voiceClient;

    bool lost = false;
    bool flushNow = false;

    std::thread thread;
};
