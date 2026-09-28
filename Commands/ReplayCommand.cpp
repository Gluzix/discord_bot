#include "ReplayCommand.h"
#include "PlaybackController.h"

#include <dpp/dpp.h>

ReplayCommand::ReplayCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("replay", "start the current song over")
    , playback(playback_)
{
}

void ReplayCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    PlaybackController::SeekResult result = playback->seekTo(0);
    if (!result.playing) {
        event.reply(messages::nothingPlaying);
    } else if (!result.seekable) {
        event.reply(messages::songStillLoading);
    } else {
        event.reply(messages::replaying);
    }
}
