#pragma once

#include <cstdint>
#include <string>

namespace dpp {
struct interaction_create_t;
}

namespace interactions {

// Discord drops an interaction 3 s after it was issued, so how much of that
// budget was already gone on arrival tells a late dispatch from a slow handler.
int64_t ageMs(const dpp::interaction_create_t &event);

// The result, shown to everyone; a click rewrites the status line under the title so the row stays put.
void reply(const dpp::interaction_create_t &event, const std::string &text);

// For the clicker's eyes only; a slash command answers the channel.
void refuse(const dpp::interaction_create_t &event, const std::string &text);

}
