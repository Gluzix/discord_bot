#pragma once

#include <string>

namespace dpp {
struct slashcommand_t;
class slashcommand;
class snowflake;
}

class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;

    // The full slash command as it should be registered with Discord.
    // The default is name + description; override to add options.
    virtual dpp::slashcommand definition(dpp::snowflake botId) const;

    virtual void execute(const dpp::slashcommand_t &event) = 0;
};
