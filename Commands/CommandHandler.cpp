#include "CommandHandler.h"
#include "Command.h"
#include "JoinCommand.h"
#include "LeaveCommand.h"
#include "StopCommand.h"
#include "SkipCommand.h"
#include "QueueCommand.h"
#include "PlayCommand.h"
#include "PlaybackController.h"
#include "PauseCommand.h"
#include "ResumeCommand.h"
#include <QDebug>

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

    add<Command>("ping", "ping test");
    add<JoinCommand>(playback);
    add<PlayCommand>(playback);
    add<StopCommand>(playback);
    add<LeaveCommand>(playback);
    add<SkipCommand>(playback);
    add<QueueCommand>(playback);
    add<PauseCommand>(playback);
    add<ResumeCommand>(playback);

    if (bot) {
        // Starts a /play that was waiting for the voice handshake to finish.
        bot->on_voice_ready([playback = playback](const dpp::voice_ready_t& event) {
            playback->onVoiceReady(event);
        });

        // Any change of the bot's own voice channel (kicked, dragged, or
        // disconnected) can destroy the old voice client - stop playback so
        // no thread keeps sending into a dead client.
        bot->on_voice_state_update([playback = playback, bot = bot](const dpp::voice_state_update_t& event) {
            if (event.state.user_id == bot->me.id) {
                playback->onBotVoiceStateChanged(event.state.channel_id);
            }
        });

        bot->on_slashcommand([this](const dpp::slashcommand_t& event) {
            auto command = commands.find(event.command.get_command_name());
            if (command != commands.end()) {
                command->second->execute(event);
            }
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
    } else {
        qDebug() << "Incorrect bot ptr";
    }
}
