#include "CommandHandler.h"
#include "Command.h"
#include "JoinCommand.h"
#include "LeaveCommand.h"
#include "StopCommand.h"
#include "SkipCommand.h"
#include "QueueCommand.h"
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
#include <QDebug>
#include <QString>

#include <chrono>
#include <cstdint>
#include <string>

namespace {

// Discord drops an interaction 3 s after it was issued, so how much of that
// budget was already gone on arrival tells a late dispatch from a slow handler.
int64_t interactionAgeMs(const dpp::slashcommand_t &event)
{
    const std::chrono::duration<double> issuedAt(event.command.id.get_creation_time());
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch() - issuedAt).count();
}

}

CommandHandler::CommandHandler()
{
}

void CommandHandler::setBot(std::shared_ptr<dpp::cluster> bot_)
{
    bot = bot_;
}

void CommandHandler::prepare()
{
    playback = std::make_shared<PlaybackController>();

    setupCommands();
    setupBot();
}

void CommandHandler::setupCommands()
{
    add<Command>("ping", "ping test");
    add<JoinCommand>(playback);
    add<PlayCommand>(playback);
    add<StopCommand>(playback);
    add<LeaveCommand>(playback);
    add<SkipCommand>(playback);
    add<QueueCommand>(playback);
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

    rejoiner = std::make_shared<VoiceRejoiner>(*bot);

    // A voice session dpp can't recover is replaced by a fresh one.
    playback->setVoiceLostHandler([rejoiner = rejoiner](uint64_t guildId, uint64_t channelId) {
        rejoiner->rejoin(guildId, channelId);
    });

    // Starts a /play that was waiting for the voice handshake to finish.
    bot->on_voice_ready([playback = playback, rejoiner = rejoiner](const dpp::voice_ready_t& event) {
        playback->onVoiceReady(event);
        if (event.voice_client) {
            rejoiner->onVoiceReady(static_cast<uint64_t>(event.voice_client->server_id));
        }
    });

    // Any change of the bot's own voice channel (kicked, dragged, or
    // disconnected) can destroy the old voice client - stop playback so
    // no thread keeps sending into a dead client.
    bot->on_voice_state_update([playback = playback, bot = bot, rejoiner = rejoiner](const dpp::voice_state_update_t& event) {
        if (event.state.user_id != bot->me.id) {
            return;
        }

        if (event.state.channel_id == 0 && rejoiner->isPending(event.state.guild_id)) {
            // A rejoin's own leave: it starts the join, and playback must not
            // see it - that would stop the song and wipe the queue.
            rejoiner->onBotLeft(event.state.guild_id);
            return;
        }

        playback->onBotVoiceStateChanged(event.state.channel_id);
    });

    // Leave voice after sitting idle (nothing playing, empty queue) for
    // too long - the bot shouldn't squat in a channel it isn't serving.
    // dpp's own connection registry is the source of truth for "in voice".
    constexpr int64_t IDLE_TIMEOUT_SECONDS = 5 * 60;
    bot->start_timer([bot = bot, playback = playback, rejoiner = rejoiner](dpp::timer) {
        rejoiner->tick();

        PlaybackController::IdleInfo info = playback->idleInfo();
        if (info.guildId == 0 || !info.idle || info.idleSinceSeconds == 0) {
            return;
        }

        if (static_cast<int64_t>(time(nullptr)) - info.idleSinceSeconds < IDLE_TIMEOUT_SECONDS) {
            return;
        }

        dpp::discord_client* shard = bot->get_shard(0);
        if (shard == nullptr || shard->get_voice(info.guildId) == nullptr) {
            return; // not connected - nothing to leave
        }

        playback->stop(); // teardown order: our threads let go first
        shard->disconnect_voice(info.guildId);
        if (info.textChannelId != 0) {
            bot->message_create(dpp::message(info.textChannelId, messages::idleLeft));
        }
    }, 30);

    bot->on_slashcommand([this](const dpp::slashcommand_t& event) {
        const QString name = QString::fromStdString("/" + event.command.get_command_name());
        auto command = commands.find(event.command.get_command_name());
        if (command == commands.end()) {
            qDebug().noquote() << name << "is not a known command, ignored";
            return;
        }

        qDebug().noquote() << name << "received" << interactionAgeMs(event)
                           << "ms after it was issued";
        const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
        command->second->execute(event);
        qDebug().noquote() << name << "handled in"
                           << std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - startedAt).count() << "ms";
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
