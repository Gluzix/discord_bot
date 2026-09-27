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

    sender.join();

    if (sender.voiceLost()) {
        decoder.join();
        return Outcome::VoiceLost;
    }

    // Flush dpp's ~1s buffer so the next song doesn't queue up behind this
    // one's tail. A song that ended on its own keeps its tail. A pause dies
    // with its song.
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

std::optional<SongPlayer::Position> SongPlayer::position() const
{
    return pcmBuffer.position();
}
