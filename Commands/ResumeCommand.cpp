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
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (currentVoiceChannel && !VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        event.reply(messages::mustBeWithBot);
        return;
    }

    if (playback->resume()) {
        event.reply(messages::resume);
    } else {
        event.reply(messages::nothingToResume);
    }
}
