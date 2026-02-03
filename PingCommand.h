#pragma once

#include "ICommand.h"

#include <utility>
#include <string>

class PingCommand : public ICommand
{
public:
    PingCommand();
    ~PingCommand();

    void prepare() override;
    std::string name() override;
    std::string reply() override;

    std::pair<std::string, std::string> command{};
};
