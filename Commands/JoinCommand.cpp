#include "JoinCommand.h"
#include "VoiceConnector.h"

#include <dpp/dpp.h>

JoinCommand::JoinCommand()
    : Command("join", "joins channel where the user's in")
{
}

void JoinCommand::execute(const dpp::slashcommand_t &event)
{
    switch (VoiceConnector::ensureJoined(event)) {
    case VoiceConnector::Result::Joined:
        event.reply("Joined your channel!");
        break;
    case VoiceConnector::Result::AlreadyInChannel:
        event.reply("Don't need to join your channel as i'm already there with you!");
        break;
    case VoiceConnector::Result::UserNotInVoice:
        event.reply("You don't seem to be in a voice channel!");
        break;
    }
}
