#include "StartTimerCommand.h"
#include <dpp/unicode_emoji.h>

StartTimerCommand::StartTimerCommand(const std::string &name, const std::string &reply, std::shared_ptr<dpp::cluster> bot_)
    : Command(name, reply)
    , bot(bot_)
{

}

void StartTimerCommand::execute(const dpp::slashcommand_t &event)
{
    if (event.command.get_command_name() == "start_timer") {
        if (user_timers.find(event.command.usr.id) != user_timers.end()) {
            event.reply("You've already got an in-progress timer!");
            return;
        }

        /* Create a copy of the channel_id to copy in to the timer lambda. */
        dpp::snowflake channel_id = event.command.channel_id;

        /* Start the timer and save it to a local variable. */
        dpp::timer timer = bot->start_timer([this, channel_id](const dpp::timer& timer) {
            bot->message_create(dpp::message(channel_id, "@everyone PAMIETAJCIE O SHADERACH W GROUNDED! " + std::string(dpp::unicode_emoji::nerd)));
        }, 240);

        /*
                 * Add the timer to user_timers.
                 * As dpp::timer is just size_t (essentially the timer's ID), it's perfectly safe to copy it in.
                 */
        user_timers.emplace(event.command.usr.id, timer);

        event.reply("Started a timer every 240 seconds!");
    }

    if(event.command.get_command_name() == "stop_timer") {
        /* Is user_timers empty? */
        if (user_timers.empty()) {
            event.reply("There are no timers currently in-progress!");
            return;
        } else if (user_timers.find(event.command.usr.id) == user_timers.end()) { /* Does user_timers not contain the user id? */
            event.reply("You've don't currently have a timer in-progress!");
            return;
        }

        /* Stop the timer. */
        bot->stop_timer(user_timers[event.command.usr.id]);
        /* Remove the timer from user_timers. */
        user_timers.erase(event.command.usr.id);

        event.reply("Stopped your timer!");
    }
}

std::string StartTimerCommand::name()
{
    return cmdName;
}

std::string StartTimerCommand::getReply()
{
    return reply;
}
