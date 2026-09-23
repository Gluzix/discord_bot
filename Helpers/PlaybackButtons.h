#pragma once

#include <optional>
#include <string>

namespace dpp {
class component;
}

// Button ids are made and read only here.
namespace buttons {

// Not a command: the router picks pause or resume for it.
inline constexpr const char* PLAY_PAUSE = "playpause";

// The row under a "Playing:" message.
dpp::component controlRow();

// "forward:10" -> {forward, 10}
struct Click
{
    std::string command;
    std::string argument;
};

// nullopt for an id that no button in the row carries.
std::optional<Click> parse(const std::string &customId);

}
