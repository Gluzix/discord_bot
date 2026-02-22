#pragma once

#include "Command.h"

#include <functional>

class LeaveCommand : public Command
{
public:
    LeaveCommand(std::string name);

    void execute(const dpp::slashcommand_t &event) override;
    std::string name() override;
    std::string getReply() override;

    void setStopPlayingFunction(const std::function<void()> &func);

private:
    std::function<void()> stopPlayingFunc;

};
