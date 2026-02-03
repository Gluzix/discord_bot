#pragma once

#include "CommandHandler.h"

#include <dpp/dpp.h>

class CtrlMainServer
{
public:
    CtrlMainServer();

    void run();

private:
    std::shared_ptr<dpp::cluster> bot;
    CommandHandler cmdHandler;
};
