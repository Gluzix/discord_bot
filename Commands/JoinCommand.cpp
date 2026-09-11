#include "JoinCommand.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>

JoinCommand::JoinCommand()
    : Command("join", "joins channel where the user's in")
{
}

void JoinCommand::execute(const dpp::slashcommand_t &event)
{
    switch (VoiceConnector::ensureJoined(event)) {
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
