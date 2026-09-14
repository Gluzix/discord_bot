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
    // onLabelResolved is called (from the decoder thread) with the rendered
    // "now playing" label once the song resolves, so /queue can show the title.
    explicit SongPlayer(std::function<void(std::string)> onLabelResolved_);

    // Resolves if needed, announces, decodes and streams one song on the
    // client. Blocks until the song ends or stop() is called. Updates song's
    // resolved fields in place if it had to re-resolve. Returns false on
    // failure (nothing was played).
    bool play(dpp::discord_voice_client *voiceClient, Song &song);

    // Ends the current song; play() returns once its threads unwind.
    void stop();

    // Discards up to `seconds` of buffered PCM. Returns the whole seconds
    // dropped (at least 1 for any real drop), 0 when nothing was buffered.
    int forward(int seconds);

private:
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void decode(Song &song);

    std::function<void(std::string)> onLabelResolved;

    std::atomic<bool> isPlaying{false};
    std::thread senderThread;
    std::thread decoderThread;

    std::queue<std::vector<uint8_t>> audioQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;

    // Written by the decoder thread, read by play() after it joins - the join
    // is the synchronisation point, so no lock is needed.
    bool currentSongFailed = false;
};
