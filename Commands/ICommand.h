#pragma once

#include <string>

namespace dpp {
struct slashcommand_t;
}

class ICommand
{
public:
    virtual ~ICommand() = default;
    virtual void prepare() = 0;
    virtual void execute(const dpp::slashcommand_t& event) = 0;
    virtual std::string name() = 0;
    virtual std::string getReply() = 0;
};
