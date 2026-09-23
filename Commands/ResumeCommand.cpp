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
    run(event);
}

void ResumeCommand::execute(const dpp::button_click_t &event, const std::string &)
{
    run(event);
}

void ResumeCommand::run(const dpp::interaction_create_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    if (playback->resume()) {
        reply(event, messages::resume);
    } else {
        reply(event, messages::nothingToResume);
    }
}
