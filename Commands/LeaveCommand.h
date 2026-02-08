#pragma once

#include "Command.h"

class LeaveCommand : public Command
{
public:
    LeaveCommand(std::string name);

    void execute(const dpp::slashcommand_t &event) override;
    std::string name() override;
    std::string getReply() override;
};
