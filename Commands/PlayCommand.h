#pragma once

#include "JoinCommand.h"

class PlayCommand : public JoinCommand
{
public:
    PlayCommand(std::string name, std::string reply);

    void execute(const dpp::slashcommand_t &event) override;
    std::string name() override;
    std::string getReply() override;
};
