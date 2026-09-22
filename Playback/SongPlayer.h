#pragma once

#include "Song.h"
#include "PcmBuffer.h"

#include <functional>
#include <optional>
#include <string>

namespace dpp {
class discord_voice_client;
}

// Commands one song: its lifecycle, its outcome and the seek API. A
// DecoderWorker and a SenderWorker do the work, sharing a PcmBuffer. Knows
// nothing about the queue or loop policy - that stays in PlaybackController.
class SongPlayer
{
public:
    enum class Outcome {
        Finished,  // played to its end, or was skipped/stopped
        Failed,    // nothing was played
        VoiceLost, // the client stopped taking audio; it must not be used again
    };

    using Position = PcmBuffer::Position;

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
    std::function<void(std::string)> onLabelResolved;

    PcmBuffer pcmBuffer;
};
