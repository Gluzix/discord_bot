#pragma once

#include "Command.h"

#include <map>
#include <dpp/dpp.h>

class StartTimerCommand : public Command
{
public:
    StartTimerCommand(const std::string &name, const std::string &reply, std::shared_ptr<dpp::cluster> bot_,
                      std::map<dpp::snowflake, dpp::timer> &userTimers_);

    void execute(const dpp::slashcommand_t &event);
    std::string name();
    std::string getReply();

private:
    std::shared_ptr<dpp::cluster> bot;
    std::map<dpp::snowflake, dpp::timer> &userTimers;

    std::string messageToRepeat{"Some example message"};
    int timeout{10}; // 10 seconds
};
