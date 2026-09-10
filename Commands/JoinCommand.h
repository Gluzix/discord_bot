#pragma once

#include "Command.h"

class JoinCommand : public Command
{
public:
    JoinCommand();

    void execute(const dpp::slashcommand_t& event) override;
};
