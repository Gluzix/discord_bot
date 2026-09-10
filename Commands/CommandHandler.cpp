#include "CommandHandler.h"
#include "Command.h"
#include "JoinCommand.h"
#include "LeaveCommand.h"
#include "StartTimerCommand.h"
#include "PlayCommand.h"
#include "StopCommand.h"
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
    commands.push_back(std::make_unique<JoinCommand>("join", "joins channel where the user's in"));

    // NEED TO RETHINK WHOLE ARCHITECTURE HERE...
    std::unique_ptr<ICommand> playCommand = std::make_unique<PlayCommand>("play", "plays audio from the given youtube link");
    std::unique_ptr<ICommand> leaveCommand = std::make_unique<LeaveCommand>("leave");
    std::unique_ptr<ICommand> stopCommand = std::make_unique<StopCommand>("stop");

    // Capture the raw pointer, not the local unique_ptr: the unique_ptr is
    // moved into `commands` below and the local dies when prepare() returns.
    PlayCommand* playCommandPtr = dynamic_cast<PlayCommand*>(playCommand.get());
    auto stopPlayingFunc = [playCommandPtr](){
        if (playCommandPtr)
            playCommandPtr->stopPlayback();
    };
    dynamic_cast<LeaveCommand*>(leaveCommand.get())->setStopPlayingFunction(stopPlayingFunc);
    dynamic_cast<StopCommand*>(stopCommand.get())->setStopPlayingFunction(stopPlayingFunc);

    commands.push_back(std::move(playCommand));
    commands.push_back(std::move(leaveCommand));
    commands.push_back(std::move(stopCommand));

    commands.push_back(std::make_unique<StartTimerCommand>("start_timer", "started timer...", bot, userTimers));
    commands.push_back(std::make_unique<StartTimerCommand>("stop_timer", "stopped timer...", bot, userTimers));

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
                    dpp::slashcommand slashCmd(cmd->name(), cmd->getReply(), bot->me.id);
                    if (dynamic_cast<PlayCommand*>(cmd.get())) {
                        slashCmd.add_option(dpp::command_option(dpp::co_string, "url", "YouTube video link", true));
                    }
                    bot->global_command_create(slashCmd);
                }
            }
        });
    } else {
        qDebug() << "Incorrect bot ptr";
    }
}
