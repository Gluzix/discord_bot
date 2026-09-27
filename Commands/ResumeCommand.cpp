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

    switch (playback->resume()) {
    case PlaybackController::PauseResult::Done:
        reply(event, messages::resume, *playback);
        break;
    case PlaybackController::PauseResult::AlreadySo:
        reply(event, messages::alreadyPlaying, *playback);
        break;
    case PlaybackController::PauseResult::NothingPlaying:
        reply(event, messages::nothingToResume, *playback);
        break;
    }
}
