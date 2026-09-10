#include "CommandHandler.h"
#include "Command.h"
#include "JoinCommand.h"
#include "LeaveCommand.h"
#include "StopCommand.h"
#include "SkipCommand.h"
#include "QueueCommand.h"
#include "StartTimerCommand.h"
#include "PlayCommand.h"
#include "PlaybackController.h"
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
    add<JoinCommand>();
    add<PlayCommand>(playback);
    add<StopCommand>(playback);
    add<LeaveCommand>(playback);
    add<SkipCommand>(playback);
    add<QueueCommand>(playback);
    add<StartTimerCommand>("start_timer", "started timer...", bot, userTimers);
    add<StartTimerCommand>("stop_timer", "stopped timer...", bot, userTimers);

    if (bot) {
        // Starts a /play that was waiting for the voice handshake to finish.
        bot->on_voice_ready([playback = playback](const dpp::voice_ready_t& event) {
            playback->onVoiceReady(event);
        });

        bot->on_slashcommand([this](const dpp::slashcommand_t& event) {
            auto command = commands.find(event.command.get_command_name());
            if (command != commands.end()) {
                command->second->execute(event);
            }
        });

        bot->on_ready([this](const dpp::ready_t& event) {
            if (dpp::run_once<struct register_bot_commands>()) {
                for (const auto &[name, command] : commands) {
                    bot->global_command_create(command->definition(bot->me.id));
                }
            }
        });
    } else {
        qDebug() << "Incorrect bot ptr";
    }
}
