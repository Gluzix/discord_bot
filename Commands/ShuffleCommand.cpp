#include "ShuffleCommand.h"
#include "PlaybackController.h"

#include <dpp/dpp.h>

ShuffleCommand::ShuffleCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("shuffle", "shuffle the waiting songs")
    , playback(playback_)
{
}

void ShuffleCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    if (playback->shuffle() < 2) {
        event.reply(messages::nothingToShuffle);
    } else {
        event.reply(messages::shuffled);
    }
}
