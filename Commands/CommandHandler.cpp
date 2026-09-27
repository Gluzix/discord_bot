#include "CommandHandler.h"
#include "Command.h"
#include "JoinCommand.h"
#include "LeaveCommand.h"
#include "StopCommand.h"
#include "SkipCommand.h"
#include "QueueCommand.h"
#include "NowPlayingCommand.h"
#include "PlayCommand.h"
#include "PlaybackController.h"
#include "Messages.h"
#include "PauseCommand.h"
#include "ResumeCommand.h"
#include "LoopCommand.h"
#include "ForwardCommand.h"
#include "RewindCommand.h"
#include "SeekCommand.h"
#include "PlaylistCommand.h"
#include "VoiceRejoiner.h"
#include "VoiceConnector.h"
#include "Interactions.h"
#include <QDebug>
#include <QString>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

CommandHandler::CommandHandler()
{
}

void CommandHandler::setBot(std::shared_ptr<dpp::cluster> bot_)
{
    bot = bot_;
}

void CommandHandler::prepare()
{
    stayWhenAlone = qEnvironmentVariable("BOT_STAY_WHEN_ALONE") == QLatin1String("1");
    if (stayWhenAlone) {
        qDebug() << "BOT_STAY_WHEN_ALONE=1 - an empty voice channel will not make the bot leave";
    }

    playback = std::make_shared<PlaybackController>();
    if (bot) {
        rejoiner = std::make_shared<VoiceRejoiner>(*bot);
    }

    setupCommands();
    buttonRouter = std::make_unique<ButtonRouter>(commands, playback);
    setupBot();
}

void CommandHandler::setupCommands()
{
    add<Command>("ping", "ping test");
    add<JoinCommand>(playback);
    add<PlayCommand>(playback);
    add<StopCommand>(playback);
    add<LeaveCommand>(playback, rejoiner);
    add<SkipCommand>(playback);
    add<QueueCommand>(playback);
    add<NowPlayingCommand>(playback);
    add<PauseCommand>(playback);
    add<ResumeCommand>(playback);
    add<LoopCommand>(playback);
    add<ForwardCommand>(playback);
    add<RewindCommand>(playback);
    add<SeekCommand>(playback);
    add<PlaylistCommand>(playback);
}

void CommandHandler::setupBot()
{
    if (!bot) {
        qDebug() << "Incorrect bot ptr";
        return;
    }

    playback->setVoiceLostHandler([rejoiner = rejoiner](uint64_t guildId, uint64_t channelId) {
        rejoiner->rejoin(guildId, channelId);
    });

    playback->setVoiceClientLookup([bot = bot](uint64_t guildId) -> dpp::discord_voice_client * {
        dpp::discord_client *shard = bot->get_shard(0);
        dpp::voiceconn *vc = shard ? shard->get_voice(guildId) : nullptr;
        // is_ready() only means the secret key arrived; it stays true all
        // through dpp's teardown, so terminating is what rules a dying one out.
        if (vc && vc->voiceclient && vc->voiceclient->is_ready() && !vc->voiceclient->terminating) {
            return vc->voiceclient.get();
        }
        return nullptr;
    });

    bot->on_voice_ready([playback = playback, rejoiner = rejoiner](const dpp::voice_ready_t& event) {
        playback->onVoiceReady(event);
        if (event.voice_client) {
            rejoiner->onVoiceReady(static_cast<uint64_t>(event.voice_client->server_id));
        }
    });

    bot->on_voice_state_update([playback = playback, bot = bot, rejoiner = rejoiner](const dpp::voice_state_update_t& event) {
        if (event.state.user_id != bot->me.id) {
            return;
        }

        if (event.state.channel_id == 0 && rejoiner->isPending(event.state.guild_id)) {
            rejoiner->onBotLeft(event.state.guild_id);
            return;
        }

        playback->onBotVoiceStateChanged(event.state.channel_id);
    });

    bot->start_timer([this](dpp::timer) {
        rejoiner->tick();

        const PlaybackController::IdleInfo info = playback->idleInfo();
        if (info.guildId == 0) {
            return;
        }

        if (!leaveIfAlone(info)) {
            leaveIfIdle(info);
        }
    }, 30);

    bot->on_slashcommand([this](const dpp::slashcommand_t& event) {
        const QString name = QString::fromStdString("/" + event.command.get_command_name());
        auto command = commands.find(event.command.get_command_name());
        if (command == commands.end()) {
            qDebug().noquote() << name << "is not a known command, ignored";
            return;
        }

        qDebug().noquote() << name << "received" << interactions::ageMs(event)
                           << "ms after it was issued";
        const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        command->second->execute(event);
        qDebug().noquote() << name << "handled in"
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - startedAt).count() << "ms";
    });

    bot->on_button_click([this](const dpp::button_click_t& event) {
        buttonRouter->handle(event);
    });

    bot->on_ready([this](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            // Bulk create OVERWRITES the guild-visible command set, so
            // commands deleted from the code also disappear from Discord
            // instead of lingering as dead entries.
            std::vector<dpp::slashcommand> definitions;
            for (const auto &[name, command] : commands) {
                definitions.push_back(command->definition(bot->me.id));
            }
            bot->global_bulk_command_create(definitions);
        }
    });
}

bool CommandHandler::leaveIfAlone(const PlaybackController::IdleInfo &info)
{
    if (stayWhenAlone) {
        return false;
    }

    switch (emptyRoomClock.observe(VoiceConnector::botChannel(*bot, info.guildId), static_cast<int64_t>(time(nullptr)))) {
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

void CommandHandler::leaveIfIdle(const PlaybackController::IdleInfo &info)
{
    if (!info.idle || info.idleSinceSeconds == 0) {
        return;
    }

    if (static_cast<int64_t>(time(nullptr)) - info.idleSinceSeconds < IDLE_TIMEOUT_SECONDS) {
        return;
    }

    dpp::discord_client* shard = bot->get_shard(0);
    if (shard == nullptr || shard->get_voice(info.guildId) == nullptr) {
        return;
    }

    qDebug() << "Nothing played for" << IDLE_TIMEOUT_SECONDS / 60 << "minutes - leaving the voice channel";
    leaveVoice(info.guildId, info.textChannelId, messages::idleLeft);
}

void CommandHandler::leaveVoice(uint64_t guildId, uint64_t textChannelId, const char *farewell)
{
    playback->stop();
    rejoiner->cancel(guildId);

    dpp::discord_client* shard = bot->get_shard(0);
    if (shard == nullptr) {
        return;
    }

    shard->disconnect_voice(guildId);
    if (textChannelId != 0) {
        bot->message_create(dpp::message(textChannelId, farewell));
    }
}
