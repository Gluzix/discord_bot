#include "SkipCommand.h"
#include "PlaybackController.h"

#include <dpp/dpp.h>

SkipCommand::SkipCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("skip", "skips the current song")
    , playback(playback_)
{
}

void SkipCommand::execute(const dpp::slashcommand_t &event)
{
    if (playback->skip()) {
        event.reply("Skipped!");
    } else {
        event.reply("Nothing is playing right now!");
    }
}
