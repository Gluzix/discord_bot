#pragma once

#include <dpp/dpp.h>
#include "ICommand.h"

class CommandHandler
{
public:
    CommandHandler();
    void setBot(std::shared_ptr<dpp::cluster> bot_);
    void prepare();

private:
    std::shared_ptr<dpp::cluster> bot;
    std::vector<std::unique_ptr<ICommand>> commands{};

    // Timers
    std::map<dpp::snowflake, dpp::timer> userTimers{};
};
