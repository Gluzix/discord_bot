#pragma once

#include "Song.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace dpp {
class discord_voice_client;
}

// The Discord-facing pipeline for one song at a time: resolve (when the
// prefetch is stale), announce, decode via PcmResampler, and pace PCM packets
// to the voice client. Owns a decoder and a sender thread per song. Knows
// nothing about the queue or loop policy - that stays in PlaybackController.
class SongPlayer
{
public:
    enum class Outcome {
        Finished,  // played to its end, or was skipped/stopped
        Failed,    // nothing was played
        VoiceLost, // the client stopped taking audio; it must not be used again
    };

    // onLabelResolved is called (from the decoder thread) with the rendered
    // "now playing" label once the song resolves, so /queue can show the title.
    explicit SongPlayer(std::function<void(std::string)> onLabelResolved_);

    // Makes the coming play() stoppable from this moment: a stop() landing
    // between arm() and play() ends the song before it starts. Call it in
    // the same critical section that publishes the song as in progress.
    void arm();

    // Resolves if needed, announces, decodes and streams one song on the
    // client. Blocks until the song ends or stop() is called. Updates song's
    // resolved fields in place if it had to re-resolve. arm() first.
    Outcome play(dpp::discord_voice_client *voiceClient, Song &song);

    // Ends the current (or armed) song; play() returns once its threads unwind.
    void stop();

    // Jumps ahead by `seconds`: what the buffer holds is dropped at once,
    // the rest the decoder skips by decoding and discarding. Returns the
    // whole seconds being skipped (at least 1 for any real jump), 0 when
    // the song has nothing left to skip.
    int forward(int seconds);

private:
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void decode(Song &song);

    std::function<void(std::string)> onLabelResolved;

    std::atomic<bool> isPlaying{false};
    std::thread senderThread;
    std::thread decoderThread;

    // The decoder runs at most MAX_QUEUED_SECONDS ahead of playback - a cap
    // on memory (a whole album as PCM is hundreds of MB), not a pacing device.
    static constexpr size_t BYTES_PER_SECOND = 192000; // 48kHz * 2ch * 2 bytes
    static constexpr size_t MAX_QUEUED_SECONDS = 60;
    static constexpr size_t MAX_QUEUED_BYTES = MAX_QUEUED_SECONDS * BYTES_PER_SECOND;

    std::queue<std::vector<uint8_t>> audioQueue;
    size_t queuedBytes = 0;    // what audioQueue holds
    size_t bytesToDiscard = 0; // forward() beyond the buffer: the decoder skips this much
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;

    // Written by the decoder thread, read by play() after it joins - the join
    // is the synchronisation point, so no lock is needed.
    bool currentSongFailed = false;

    // Same, for the sender thread.
    bool voiceLost = false;
};
