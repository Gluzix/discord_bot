#pragma once

#include <cstdint>
#include <string>

namespace dpp {
struct interaction_create_t;
}

// Answers slash commands and button clicks from one place.
// =======================================================
// Rules:
// - isClick(), behind reply() and refuse(), counts every component
//   interaction as a click, not only a button's - fine while buttons are the
//   only components this bot sends.
// - tokenAlive() gives a token 14 minutes from the time in the interaction's
//   id: Discord keeps it 15, and the minute covers a request still queued in
//   dpp and a clock that is off.
// =======================================================
namespace interactions {

// Discord drops an interaction 3 s after it was issued, so how much of that
// budget was already gone on arrival tells a late dispatch from a slow handler.
int64_t ageMs(const dpp::interaction_create_t &event);

// Whether the reply can still be edited at nowSeconds, a unix time.
bool tokenAlive(const dpp::interaction_create_t &event, int64_t nowSeconds);

// The result, shown to everyone; a click rewrites the status line under the title so the row stays put.
// freshTitle, when given, replaces the clicked message's first line.
void reply(const dpp::interaction_create_t &event, const std::string &text,
           const std::string &freshTitle = {});

// For the clicker's eyes only; a slash command answers the channel.
void refuse(const dpp::interaction_create_t &event, const std::string &text);

}
