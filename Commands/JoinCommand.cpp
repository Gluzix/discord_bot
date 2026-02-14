#include "JoinCommand.h"

#include <dpp/dpp.h>

JoinCommand::JoinCommand(std::string name, std::string reply)
    : Command(name, reply)
{
}

JoinCommand::~JoinCommand()
{

}

void JoinCommand::execute(const dpp::slashcommand_t &event)
{
    dpp::guild* guild = dpp::find_guild(event.command.guild_id);
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    bool joinedVoiceChannel = true;

    if (currentVoiceChannel) {
        auto usersVcIterator = guild->voice_members.find(event.command.get_issuing_user().id);

        if (usersVcIterator != guild->voice_members.end() && currentVoiceChannel->channel_id == usersVcIterator->second.channel_id) {
            joinedVoiceChannel = false;
        } else {
            event.from()->disconnect_voice(event.command.guild_id);
            joinedVoiceChannel = true;
        }
    }

    if (joinedVoiceChannel) {
        if (!guild->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
            event.reply("You don't seem to be in a voice channel!");
            return;
        }
        event.reply("Joined your channel!");
    } else {
        event.reply("Don't need to join your channel as i'm already there with you!");
    }
}

std::string JoinCommand::name()
{
    return cmdName;
}

std::string JoinCommand::getReply()
{
    return reply;
}
