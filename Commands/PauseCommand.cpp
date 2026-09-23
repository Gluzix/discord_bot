#include "PauseCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>

PauseCommand::PauseCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("pause", "pause currently played song")
    , playback(playback_)
{

}

void PauseCommand::execute(const dpp::slashcommand_t &event)
{
    run(event);
}

void PauseCommand::execute(const dpp::button_click_t &event, const std::string &)
{
    run(event);
}

void PauseCommand::run(const dpp::interaction_create_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    if (playback->pause()) {
        reply(event, messages::pause);
    } else {
        reply(event, messages::nothingPlaying);
    }
}
