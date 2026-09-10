#include "Command.h"

#include <dpp/dpp.h>

Command::Command(std::string name_, std::string description_)
    : cmdName(std::move(name_))
    , cmdDescription(std::move(description_))
{

}

Command::~Command()
{

}

std::string Command::name() const
{
    return cmdName;
}

std::string Command::description() const
{
    return cmdDescription;
}

void Command::execute(const dpp::slashcommand_t &event)
{
    event.reply(cmdDescription);
}
