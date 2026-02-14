#pragma once

#include "Command.h"

class JoinCommand : public Command
{
public:
    JoinCommand(std::string name, std::string reply);
    ~JoinCommand();

    void execute(const dpp::slashcommand_t& event) override;
    std::string name() override;
    std::string getReply() override;
};
