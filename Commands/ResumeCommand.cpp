#include "ResumeCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>


ResumeCommand::ResumeCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("resume", "resuming currently paused song")
    , playback(playback_)
{

}

void ResumeCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    if (playback->resume()) {
        event.reply(messages::resume);
    } else {
        event.reply(messages::nothingToResume);
    }
}
