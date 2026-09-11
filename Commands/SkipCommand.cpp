#include "SkipCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>

SkipCommand::SkipCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("skip", "skips the current song")
    , playback(playback_)
{
}

void SkipCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    if (playback->skip()) {
        event.reply(messages::skipped);
    } else {
        event.reply(messages::nothingPlaying);
    }
}
