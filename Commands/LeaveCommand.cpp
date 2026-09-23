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

    if (!userMayControl(event, messages::cannotLeave)) {
        return;
    }

    playback->stop();
    if (rejoiner) {
        rejoiner->cancel(event.command.guild_id);
    }
    event.from()->disconnect_voice(event.command.guild_id);

    event.reply(messages::leaving);
}
