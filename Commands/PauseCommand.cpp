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
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (currentVoiceChannel && !VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        event.reply(messages::mustBeWithBot);
        return;
    }

    if (playback->pause()) {
        event.reply(messages::pause);
    } else {
        event.reply(messages::nothingPlaying);
    }
}
