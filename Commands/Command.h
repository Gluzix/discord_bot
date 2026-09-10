#pragma once

#include "ICommand.h"

#include <string>

class Command : public ICommand
{
public:
    Command(const Command& other) = delete;
    Command(Command&& other) = delete;
    Command() = delete;
    Command& operator=(const Command& other) = delete;

    Command(std::string name_, std::string description_);
    ~Command() override;

    std::string name() const override;
    std::string description() const override;

    // Generic behavior: reply with the description text.
    void execute(const dpp::slashcommand_t &event) override;

protected:
    std::string cmdName{};
    std::string cmdDescription{};
};
