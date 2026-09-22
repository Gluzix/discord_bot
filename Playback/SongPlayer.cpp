#include "SongPlayer.h"
#include "DecoderWorker.h"
#include "SenderWorker.h"

#include <dpp/dpp.h>

SongPlayer::SongPlayer(std::function<void(std::string)> onLabelResolved_)
    : onLabelResolved(std::move(onLabelResolved_))
{
}

void SongPlayer::arm()
{
    pcmBuffer.arm();
}

SongPlayer::Outcome SongPlayer::play(dpp::discord_voice_client *voiceClient, Song &song)
{
    pcmBuffer.reset();

    DecoderWorker decoder(pcmBuffer, song, onLabelResolved);
    SenderWorker sender(pcmBuffer, voiceClient);

    // Sender first: once it is joined, this thread is the only one on the
    // voice client, so this is the one place that may call it after a song.
    // It has to happen before the decoder join - a decoder stuck in yt-dlp
    // can outlive stop()'s bounded wait, and past that the client may be gone.
    sender.join();

    // dpp may already have destroyed the client it gave up on - not one
    // call more, not even is_paused().
    if (sender.voiceLost()) {
        decoder.join();
        return Outcome::VoiceLost;
    }

    // Leftover audio means the song was cut short (skip/stop): flush dpp's
    // ~1s buffer so the next song doesn't queue up behind this one's tail.
    // A song that ended on its own keeps its tail. A pause dies with its song.
    if (pcmBuffer.cutShort()) {
        voiceClient->stop_audio();
    }
    if (voiceClient->is_paused()) {
        voiceClient->pause_audio(false);
    }

    decoder.join();
    return decoder.failed() ? Outcome::Failed : Outcome::Finished;
}

void SongPlayer::stop()
{
    pcmBuffer.stop();
}

std::optional<SongPlayer::Position> SongPlayer::seekBy(int deltaSeconds)
{
    return pcmBuffer.seek(deltaSeconds, true);
}

std::optional<SongPlayer::Position> SongPlayer::seekTo(int seconds)
{
    return pcmBuffer.seek(seconds, false);
}
