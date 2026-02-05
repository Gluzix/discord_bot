#pragma once

#include "Command.h"

class JoinCommand : public Command
{
public:
    JoinCommand(std::string name);
    ~JoinCommand();

    void execute() override;
    std::string name() override;
    std::string getReply() override;
};
