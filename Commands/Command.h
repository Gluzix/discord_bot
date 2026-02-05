#pragma once

#include "ICommand.h"

#include <dpp/dpp.h>

#include <utility>
#include <string>

class Command : public ICommand
{
public:
    Command(const Command& other) = delete;
    Command(const Command&& other) = delete;
    Command() = delete;
    Command& operator=(const Command& other) = delete;

    Command(std::string name_, std::string defaultReply_ = "");
    ~Command();

    void setReply(const std::string &reply_);

    void execute() override;
    void prepare() override;
    std::string name() override;
    std::string getReply() override;

protected:
    std::string cmdName{};
    std::string reply{};
};
