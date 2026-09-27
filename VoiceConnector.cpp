#include "VoiceConnector.h"
#include "PlaybackController.h"

#include <dpp/dpp.h>

static bool someoneElseInChannel(const dpp::guild &guild, dpp::snowflake channelId, dpp::snowflake botId)
{
    for (const auto &[userId, voiceState] : guild.voice_members) {
        if (voiceState.channel_id == channelId && userId != botId) {
            return true;
        }
    }
    return false;
}

VoiceConnector::Result VoiceConnector::ensureJoined(const dpp::slashcommand_t &event, PlaybackController *playback)
{
    dpp::guild* guild = dpp::find_guild(event.command.guild_id);
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);

    if (currentVoiceChannel) {
        auto usersVcIterator = guild->voice_members.find(event.command.get_issuing_user().id);

        if (usersVcIterator != guild->voice_members.end() && currentVoiceChannel->channel_id == usersVcIterator->second.channel_id) {
            return Result::AlreadyInChannel;
        }

        if (playback) {
            playback->stop();
        }
    }

    if (!guild->connect_member_voice(*event.owner, event.command.get_issuing_user().id, voice::SELF_MUTE, voice::SELF_DEAF)) {
        return Result::UserNotInVoice;
    }
    return Result::Joined;
}

bool VoiceConnector::botIsAloneInChannel(const dpp::interaction_create_t &event)
{
    dpp::guild* guild = dpp::find_guild(event.command.guild_id);
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (!guild || !currentVoiceChannel) {
        return true;
    }

    return !someoneElseInChannel(*guild, currentVoiceChannel->channel_id, event.owner->me.id);
}

bool VoiceConnector::userInBotChannel(const dpp::interaction_create_t &event)
{
    dpp::guild* guild = dpp::find_guild(event.command.guild_id);
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (!guild || !currentVoiceChannel) {
        return false;
    }

    auto usersVcIterator = guild->voice_members.find(event.command.get_issuing_user().id);
    return usersVcIterator != guild->voice_members.end()
        && currentVoiceChannel->channel_id == usersVcIterator->second.channel_id;
}

EmptyRoomClock::Channel VoiceConnector::botChannel(dpp::cluster &bot, uint64_t guildId)
{
    dpp::discord_client* shard = bot.get_shard(0);
    dpp::voiceconn* currentVoiceChannel = shard ? shard->get_voice(guildId) : nullptr;
    if (!currentVoiceChannel) {
        return EmptyRoomClock::Channel::NotInVoice;
    }
    const dpp::snowflake channelId = currentVoiceChannel->channel_id;

    dpp::guild* guild = dpp::find_guild(guildId);
    if (!guild) {
        return EmptyRoomClock::Channel::NotInVoice;
    }

    return someoneElseInChannel(*guild, channelId, bot.me.id)
        ? EmptyRoomClock::Channel::Occupied
        : EmptyRoomClock::Channel::Empty;
}
