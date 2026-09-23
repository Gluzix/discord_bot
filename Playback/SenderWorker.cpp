#include "SenderWorker.h"
#include "Log.h"

#include <dpp/dpp.h>

#include <chrono>

// Everything handed to DPP is opus-encoded and (with DAVE E2EE, the
// default) encrypted with the *current* group key immediately. The key
// rotates whenever someone joins or leaves, turning any large queued
// backlog into silence for the listeners - so keep DPP's queue short
// and hold the deep buffer here as PCM, which no rekey can spoil.
constexpr float MAX_BUFFERED_SECONDS = 1.0f;

constexpr std::chrono::seconds VOICE_DEAD_AFTER(10);
constexpr std::chrono::milliseconds PACING_WAIT_MS(100);

SenderWorker::SenderWorker(PcmBuffer &buffer_, dpp::discord_voice_client *voiceClient_)
    : watchdog(VOICE_DEAD_AFTER)
    , buffer(buffer_)
    , voiceClient(voiceClient_)
{
    thread = std::thread(&SenderWorker::run, this);
}

SenderWorker::~SenderWorker()
{
    join();
}

void SenderWorker::join()
{
    if (thread.joinable()) {
        thread.join();
    }
}

bool SenderWorker::voiceLost() const
{
    return lost;
}

void SenderWorker::run()
{
    logging::nameThisThread("sender");

    // The decoder fills the queue with ready-to-send packets of exactly
    // dpp::send_audio_raw_max_length bytes; only the final one may be shorter.
    while (buffer.running()) {
        PcmBuffer::Next taken = buffer.next();
        if (taken.kind == PcmBuffer::Next::Kind::Stopped || taken.kind == PcmBuffer::Next::Kind::Ended) {
            break;
        }
        if (taken.kind == PcmBuffer::Next::Kind::Flush) {
            if (!flushClientNow()) break;
            continue;
        }

        std::vector<uint8_t> packet = std::move(taken.packet);

        flushNow = false;

        waitForClientToDrain();

        if (lost) {
            buffer.stop(); // a decoder waiting on a full queue has nobody else to wake it
        }
        if (!buffer.running()) break;
        if (flushNow) {
            // The packet in hand is from before the jump - drop it.
            if (!flushClientNow()) break;
            continue;
        }

        if (voiceClient->terminating) {
            lost = true;
            buffer.stop();
            break;
        }
        voiceClient->send_audio_raw((uint16_t*)packet.data(), packet.size());
        watchdog.reset();
    }

    // Not a plain store: the decoder waits inside the buffer until the song ends.
    buffer.stop();
}

// This thread owns the client, so it is the one that may flush it.
bool SenderWorker::flushClientNow()
{
    // dpp sets terminating at least 100ms before it destroys the client.
    if (voiceClient->terminating) {
        lost = true;
        buffer.stop();
        return false;
    }
    voiceClient->stop_audio();
    return true;
}

void SenderWorker::waitForClientToDrain()
{
    // Never call into dpp while holding the buffer's mutex - a foreign lock
    // inside our critical section is how the whole pipeline wedges.
    while (buffer.running()) {
        if (voiceClient->terminating) {
            lost = true;
            break;
        }
        const float bufferedSeconds = voiceClient->get_secs_remaining();
        if (watchdog.observe(bufferedSeconds, voiceClient->is_paused())) {
            lost = true;
            break;
        }
        if (buffer.takeFlushRequest()) {
            flushNow = true;
            break;
        }
        if (bufferedSeconds <= MAX_BUFFERED_SECONDS) {
            break;
        }

        buffer.pacingWait(PACING_WAIT_MS);
    }
}
