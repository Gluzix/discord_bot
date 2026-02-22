#include "LeaveCommand.h"

#include <dpp/dpp.h>

LeaveCommand::LeaveCommand(std::string name)
    : Command(name)
{
    reply = "I will leave your channel!";
}

void LeaveCommand::execute(const dpp::slashcommand_t &event)
{
    dpp::guild* guild = dpp::find_guild(event.command.guild_id);
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);

    /* The user issuing the command is not on any voice channel, we can't do anything */
    if (!guild->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
        event.reply("Cannot leave, I'm not on the same channel as you!");
        return;
    }

    if (currentVoiceChannel) {
        auto usersVcIterator = guild->voice_members.find(event.command.get_issuing_user().id);

        if (usersVcIterator != guild->voice_members.end() && currentVoiceChannel->channel_id == usersVcIterator->second.channel_id) {

            if (currentVoiceChannel->voiceclient->is_playing()) {
                stopPlayingFunc();
                currentVoiceChannel->voiceclient->stop_audio();
            }

            // Check if bot plays an audio, if yes, stop it before leaving
            auto async = std::async([currentVoiceChannel, event]() {
                while(currentVoiceChannel->voiceclient->is_playing()) {
                    Sleep(500);
                }
                event.from()->disconnect_voice(event.command.guild_id);
                event.reply("Okay, i'm leaving :(");
            });
            async.wait();
        }
    } else {
        event.reply("Cannot leave, I'm not on the same channel as you!");
    }
}

std::string LeaveCommand::name()
{
    return cmdName;
}

std::string LeaveCommand::getReply()
{
    return reply;
}

void LeaveCommand::setStopPlayingFunction(const std::function<void ()> &func)
{
    stopPlayingFunc = func;
}
