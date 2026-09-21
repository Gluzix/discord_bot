#pragma once

#include "Song.h"
#include "PcmBuffer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace dpp {
class discord_voice_client;
}

class PcmResampler;

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

    struct Position
    {
        double seconds{0};
        double durationSeconds{0}; // 0 = unknown
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

    // Relative and absolute jumps in the current song, reporting where
    // playback landed. nullopt when there is nothing to seek in right now:
    // no song, still resolving/opening, or the song is already ending.
    std::optional<Position> seekBy(int deltaSeconds);
    std::optional<Position> seekTo(int seconds);

private:
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void decode(Song &song);

    // The one seek path: `seconds` is added to the current position when
    // relative, otherwise it is the target itself.
    std::optional<Position> seek(double seconds, bool relative);

    std::function<void(std::string)> onLabelResolved;

    std::thread senderThread;
    std::thread decoderThread;

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
    PcmResampler *activeResampler = nullptr; // only while the decoder has one open
    double durationSeconds = 0;   // 0 = the container didn't say
    bool decodingFinished = false;

    // Written by the decoder thread, read by play() after it joins - the join
    // is the synchronisation point, so no lock is needed.
    bool currentSongFailed = false;

    // Same, for the sender thread.
    bool voiceLost = false;

    PcmBuffer pcmBuffer;
};
