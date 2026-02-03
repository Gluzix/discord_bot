#include "CommandHandler.h"
#include "PingCommand.h"
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
    if (bot) {
        commands.push_back(std::make_unique<PingCommand>());

        for (auto &cmd : commands)
        {
            bot->on_slashcommand([&cmd](const dpp::slashcommand_t& event) {
                if (event.command.get_command_name() == cmd->name()) {
                    event.reply(cmd->reply());
                }
            });

            bot->on_ready([&cmd, this](const dpp::ready_t& event) {
                if (dpp::run_once<struct register_bot_commands>()) {
                    bot->global_command_create(dpp::slashcommand(cmd->name(), cmd->reply(), bot->me.id));
                }
            });
        }
    } else {
        qDebug() << "Incorrect bot ptr";
    }
}
