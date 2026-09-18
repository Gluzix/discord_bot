#include "LeaveCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "VoiceRejoiner.h"
#include "Messages.h"

#include <dpp/dpp.h>

LeaveCommand::LeaveCommand(std::shared_ptr<PlaybackController> playback_,
                           std::shared_ptr<VoiceRejoiner> rejoiner_)
    : Command("leave", "I will leave your channel!")
    , playback(playback_)
    , rejoiner(rejoiner_)
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
    if (!userMayControl(event, messages::cannotLeave)) {
        return;
    }

    // stop() joins the playback threads, so nothing of ours still touches
    // the voice client when the disconnect destroys it.
    playback->stop();
    // Before the disconnect: the state update it causes must not be taken
    // for a rejoin's own leave, or the bot would come straight back.
    if (rejoiner) {
        rejoiner->cancel(event.command.guild_id);
    }
    event.from()->disconnect_voice(event.command.guild_id);

    event.reply(messages::leaving);
}
