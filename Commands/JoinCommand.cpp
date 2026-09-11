#include "JoinCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>

JoinCommand::JoinCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("join", "joins channel where the user's in")
    , playback(playback_)
{
}

void JoinCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMaySummon(event, *playback)) {
        return;
    }

    switch (VoiceConnector::ensureJoined(event, playback.get())) {
    case VoiceConnector::Result::Joined:
        event.reply(messages::joinedChannel);
        break;
    case VoiceConnector::Result::AlreadyInChannel:
        event.reply(messages::alreadyInChannel);
        break;
    case VoiceConnector::Result::UserNotInVoice:
        event.reply(messages::userNotInVoice);
        break;
    }
}
