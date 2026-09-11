#pragma once

#include "CommandHandler.h"

#include <dpp/dpp.h>

#include <atomic>

class CtrlMainServer
{
public:
    CtrlMainServer();

    void run();

    // Asks the dpp cluster to shut down; run() then returns and the normal
    // destructor chain tears everything down. Safe from any thread, and
    // safe to call more than once (Ctrl+C mashing).
    void requestShutdown();

private:
    std::shared_ptr<dpp::cluster> bot;
    CommandHandler cmdHandler;
    std::atomic<bool> shuttingDown{false};
};
