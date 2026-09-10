#include "JoinCommand.h"
#include "VoiceConnector.h"

#include <dpp/dpp.h>

JoinCommand::JoinCommand()
    : Command("join", "joins channel where the user's in")
{
}

void JoinCommand::execute(const dpp::slashcommand_t &event)
{
    VoiceConnector::ensureJoined(event);
}
