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
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (currentVoiceChannel && !VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        event.reply(messages::mustBeWithBot);
        return;
    }

    if (playback->skip()) {
        event.reply(messages::skipped);
    } else {
        event.reply(messages::nothingPlaying);
    }
}
