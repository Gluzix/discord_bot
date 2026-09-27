#include "VoiceLeaver.h"
#include "VoiceConnector.h"
#include "VoiceRejoiner.h"
#include "Messages.h"

#include <dpp/dpp.h>
#include <QDebug>
#include <QString>

#include <ctime>

VoiceLeaver::VoiceLeaver(dpp::cluster &bot_, std::shared_ptr<PlaybackController> playback_,
                         std::shared_ptr<VoiceRejoiner> rejoiner_)
    : bot(bot_)
    , playback(playback_)
    , rejoiner(rejoiner_)
{
    stayWhenAlone = qEnvironmentVariable("BOT_STAY_WHEN_ALONE") == QLatin1String("1");
    if (stayWhenAlone) {
        qDebug() << "BOT_STAY_WHEN_ALONE=1 - an empty voice channel will not make the bot leave";
    }
}

void VoiceLeaver::tick()
{
    const PlaybackController::IdleInfo info = playback->idleInfo();
    if (info.guildId == 0) {
        return;
    }

    if (!leaveIfAlone(info)) {
        leaveIfIdle(info);
    }
}

bool VoiceLeaver::leaveIfAlone(const PlaybackController::IdleInfo &info)
{
    if (stayWhenAlone) {
        return false;
    }

    switch (emptyRoomClock.observe(VoiceConnector::botChannel(bot, info.guildId), static_cast<int64_t>(time(nullptr)))) {
    case EmptyRoomClock::Verdict::Stay:
        return false;
    case EmptyRoomClock::Verdict::ClockStarted:
        qDebug() << "Alone in the voice channel - leaving in" << ALONE_TIMEOUT_SECONDS / 60
                 << "minutes unless someone joins";
        return false;
    case EmptyRoomClock::Verdict::ClockStopped:
        qDebug() << "No longer alone in the voice channel - staying";
        return false;
    case EmptyRoomClock::Verdict::Leave:
        break;
    }

    qDebug() << "Alone in the voice channel for" << ALONE_TIMEOUT_SECONDS / 60 << "minutes - leaving";
    leaveVoice(info.guildId, info.textChannelId, messages::aloneLeft);
    return true;
}

void VoiceLeaver::leaveIfIdle(const PlaybackController::IdleInfo &info)
{
    if (!info.idle || info.idleSinceSeconds == 0) {
        return;
    }

    if (static_cast<int64_t>(time(nullptr)) - info.idleSinceSeconds < IDLE_TIMEOUT_SECONDS) {
        return;
    }

    dpp::discord_client* shard = bot.get_shard(0);
    if (shard == nullptr || shard->get_voice(info.guildId) == nullptr) {
        return;
    }

    qDebug() << "Nothing played for" << IDLE_TIMEOUT_SECONDS / 60 << "minutes - leaving the voice channel";
    leaveVoice(info.guildId, info.textChannelId, messages::idleLeft);
}

void VoiceLeaver::leaveVoice(uint64_t guildId, uint64_t textChannelId, const char *farewell)
{
    playback->stop();
    rejoiner->cancel(guildId);

    dpp::discord_client* shard = bot.get_shard(0);
    if (shard == nullptr) {
        return;
    }

    shard->disconnect_voice(guildId);
    if (textChannelId != 0) {
        bot.message_create(dpp::message(textChannelId, farewell));
    }
}
