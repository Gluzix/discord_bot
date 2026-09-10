#include "VoiceConnector.h"

#include <dpp/dpp.h>

VoiceConnector::Result VoiceConnector::ensureJoined(const dpp::slashcommand_t &event)
{
    dpp::guild* guild = dpp::find_guild(event.command.guild_id);
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);

    if (currentVoiceChannel) {
        auto usersVcIterator = guild->voice_members.find(event.command.get_issuing_user().id);

        if (usersVcIterator != guild->voice_members.end() && currentVoiceChannel->channel_id == usersVcIterator->second.channel_id) {
            return Result::AlreadyInChannel;
        }
        // Connected somewhere else - move to the user's channel.
        event.from()->disconnect_voice(event.command.guild_id);
    }

    if (!guild->connect_member_voice(*event.owner, event.command.get_issuing_user().id)) {
        return Result::UserNotInVoice;
    }
    return Result::Joined;
}
