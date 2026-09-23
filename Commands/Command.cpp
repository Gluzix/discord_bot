#include "Command.h"
#include "VoiceConnector.h"
#include "PlaybackController.h"
#include "Interactions.h"

#include <dpp/dpp.h>

Command::Command(std::string name_, std::string description_)
    : cmdName(std::move(name_))
    , cmdDescription(std::move(description_))
{

}

Command::~Command()
{

}

std::string Command::name() const
{
    return cmdName;
}

std::string Command::description() const
{
    return cmdDescription;
}

void Command::execute(const dpp::slashcommand_t &event)
{
    event.reply(cmdDescription);
}

void Command::execute(const dpp::button_click_t &event, const std::string &)
{
    interactions::refuse(event, messages::unknownButton);
}

void Command::reply(const dpp::interaction_create_t &event, const std::string &text)
{
    interactions::reply(event, text);
}

bool Command::userMayControl(const dpp::interaction_create_t &event, const char *refusalReply)
{
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (currentVoiceChannel && !VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        interactions::refuse(event, refusalReply);
        return false;
    }
    return true;
}

bool Command::userMaySummon(const dpp::slashcommand_t &event, PlaybackController &playback)
{
    PlaybackController::SessionInfo session = playback.sessionInfo();
    if (session.active && !VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        std::string where = session.channelId != 0
            ? "<#" + std::to_string(session.channelId) + ">"
            : messages::busyChannelFallback;
        event.reply(messages::busyInChannelPrefix + where + messages::busyInChannelSuffix);
        return false;
    }
    return true;
}
