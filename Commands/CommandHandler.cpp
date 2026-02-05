#include "CommandHandler.h"
#include "Command.h"
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
    commands.push_back(std::make_unique<Command>("join", "joined voice channel"));
    commands.push_back(std::make_unique<Command>("leave", "leave voice channel"));
    commands.push_back(std::make_unique<Command>("play", "play music"));
    commands.push_back(std::make_unique<Command>("stop", "stop playing music"));

    if (bot) {
        bot->on_slashcommand([this](const dpp::slashcommand_t& event) {
            for (const auto &cmd: commands)
            {
                if (event.command.get_command_name()== cmd->name())
                {
                    event.reply(cmd->getReply());
                    break;
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
