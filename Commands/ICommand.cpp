#include "ICommand.h"

#include <dpp/dpp.h>

dpp::slashcommand ICommand::definition(dpp::snowflake botId) const
{
    return dpp::slashcommand(name(), description(), botId);
}
