#include "CommandHandler.h"
#include "Command.h"
#include "JoinCommand.h"
#include "LeaveCommand.h"
#include "StartTimerCommand.h"
#include "PlayCommand.h"
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
    commands.push_back(std::make_unique<Command>("ping", "ping test"));
    commands.push_back(std::make_unique<Command>("stop", "stop playing music"));
    commands.push_back(std::make_unique<JoinCommand>("join", "joins channel where the user's in"));
    commands.push_back(std::make_unique<LeaveCommand>("leave"));
    commands.push_back(std::make_unique<StartTimerCommand>("start_timer", "started timer...", bot, userTimers));
    commands.push_back(std::make_unique<StartTimerCommand>("stop_timer", "stopped timer...", bot, userTimers));
    commands.push_back(std::make_unique<PlayCommand>("play", "play chosen song, provide with the name"));

    if (bot) {
        bot->on_slashcommand([this](const dpp::slashcommand_t& event) {
            for (const auto &cmd: commands)
            {
                if (event.command.get_command_name() == cmd->name())
                {
                    cmd->execute(event);
                }
            }
        });

        bot->on_ready([this](const dpp::ready_t& event) {
            if (dpp::run_once<struct register_bot_commands>()) {
                for (const auto &cmd: commands)
                {
                    bot->global_command_create(dpp::slashcommand(cmd->name(), cmd->getReply(), bot->me.id));
                }
            }
        });
    } else {
        qDebug() << "Incorrect bot ptr";
    }
}
