#pragma once

#include "Command.h"

#include <map>
#include <dpp/dpp.h>

class StartTimerCommand : public Command
{
public:
    StartTimerCommand(const std::string &name, const std::string &description, std::shared_ptr<dpp::cluster> bot_,
                      std::map<dpp::snowflake, dpp::timer> &userTimers_);

    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<dpp::cluster> bot;
    std::map<dpp::snowflake, dpp::timer> &userTimers;

    std::string messageToRepeat{"Some example message"};
    int timeout{10}; // 10 seconds
};
