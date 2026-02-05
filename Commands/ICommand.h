#pragma once

#include <string>

class ICommand
{
public:
    virtual ~ICommand() = default;
    virtual void prepare() = 0;
    virtual void execute() = 0;
    virtual std::string name() = 0;
    virtual std::string getReply() = 0;
};
