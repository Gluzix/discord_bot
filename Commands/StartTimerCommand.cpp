#include "StartTimerCommand.h"
#include "Messages.h"

#include <dpp/unicode_emoji.h>

StartTimerCommand::StartTimerCommand(const std::string &name, const std::string &description, std::shared_ptr<dpp::cluster> bot_,
                                     std::map<dpp::snowflake, dpp::timer> &userTimers_)
    : Command(name, description)
    , bot(bot_)
    , userTimers(userTimers_)
{

}

void StartTimerCommand::execute(const dpp::slashcommand_t &event)
{
    if (event.command.get_command_name() == "start_timer") {
        if (userTimers.find(event.command.usr.id) != userTimers.end()) {
            event.reply(messages::timerAlreadyRunning);
            return;
        }

        /* Create a copy of the channel_id to copy in to the timer lambda. */
        dpp::snowflake channelId = event.command.channel_id;

        /* Start the timer and save it to a local variable. */
        dpp::timer timer = bot->start_timer([this, channelId](const dpp::timer& timer) {
            bot->message_create(dpp::message(channelId, messageToRepeat));
        }, timeout);

        /*
                 * Add the timer to user_timers.
                 * As dpp::timer is just size_t (essentially the timer's ID), it's perfectly safe to copy it in.
                 */
        userTimers.emplace(event.command.usr.id, timer);

        event.reply(messages::timerStartedPrefix + std::to_string(timeout) + messages::timerStartedSuffix);
    }

    if(event.command.get_command_name() == "stop_timer") {
        /* Is user_timers empty? */
        if (userTimers.empty()) {
            event.reply(messages::timerNoneInProgress);
            return;
        } else if (userTimers.find(event.command.usr.id) == userTimers.end()) { /* Does user_timers not contain the user id? */
            event.reply(messages::timerNotYours);
            return;
        }

        /* Stop the timer. */
        bot->stop_timer(userTimers[event.command.usr.id]);
        /* Remove the timer from user_timers. */
        userTimers.erase(event.command.usr.id);

        event.reply(messages::timerStopped);
    }
}
