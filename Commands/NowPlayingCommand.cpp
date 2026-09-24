#include "NowPlayingCommand.h"
#include "PlaybackController.h"
#include "PlaybackButtons.h"
#include "TimeText.h"
#include "Messages.h"

#include <dpp/dpp.h>

NowPlayingCommand::NowPlayingCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("nowplaying", "what is playing and where it is")
    , playback(playback_)
{
}

void NowPlayingCommand::execute(const dpp::slashcommand_t &event)
{
    PlaybackController::NowPlaying now = playback->nowPlaying();

    if (!now.playing) {
        event.reply(messages::nothingPlaying);
        return;
    }

    std::string text = messages::queueNowPlayingPrefix + ("**" + now.label + "**");
    if (now.positionKnown) {
        text += " - " + timetext::formatProgress(now.positionSeconds, now.durationSeconds);
    }
    if (now.paused) {
        text += messages::nowPlayingPausedSuffix;
    }
    if (now.loop == PlaybackController::LoopMode::Song) {
        text += messages::queueLoopSongSuffix;
    } else if (now.loop == PlaybackController::LoopMode::Queue) {
        text += messages::queueLoopQueueSuffix;
    }

    // The label is untrusted input - never let it ping anyone.
    dpp::message msg(text);
    msg.set_allowed_mentions();
    msg.add_component(buttons::controlRow());
    event.reply(msg);
}
