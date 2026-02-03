#include "PingCommand.h"

PingCommand::PingCommand()
{
    prepare();
}

PingCommand::~PingCommand()
{

}

void PingCommand::prepare()
{
    command = {"ping", "pong"};
}

std::string PingCommand::name()
{
    return command.first;
}

std::string PingCommand::reply()
{
    return command.second;
}
