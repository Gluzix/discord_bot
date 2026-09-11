#include "LeaveCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>

LeaveCommand::LeaveCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("leave", "I will leave your channel!")
    , playback(playback_)
{
}

void LeaveCommand::execute(const dpp::slashcommand_t &event)
{
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (!currentVoiceChannel) {
        event.reply(messages::notConnected);
        return;
    }

    // Same policy as the rest of playback control: only the bot's audience
    // may dismiss it - unless it sits in an empty room anyway.
    if (!VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        event.reply(messages::cannotLeave);
        return;
    }

    // stop() joins the playback threads, so nothing of ours still touches
    // the voice client when the disconnect destroys it.
    playback->stop();
    event.from()->disconnect_voice(event.command.guild_id);

    event.reply(messages::leaving);
}
