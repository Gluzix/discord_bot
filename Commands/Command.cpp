#include "Command.h"

#include <dpp/dpp.h>

Command::Command(std::string name_, std::string defaultReply_)
    : cmdName(name_)
    , reply(defaultReply_)
{

}

void Command::setReply(const std::string &reply_)
{
    reply = reply_;
}

Command::~Command()
{

}

void Command::execute(const dpp::slashcommand_t &event)
{
    event.reply(getReply());
}

void Command::prepare()
{
}

std::string Command::name()
{
    return cmdName;
}

std::string Command::getReply()
{
    return reply;
}
