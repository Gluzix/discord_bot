#include "PlaybackButtons.h"
#include "Check.h"

#include <dpp/dpp.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <optional>
#include <string>

static bool parsesTo(const std::string &id, const char *command, const char *argument)
{
    const std::optional<buttons::Click> click = buttons::parse(id);
    return click && click->command == command && click->argument == argument;
}

static bool unknown(const std::string &id)
{
    return !buttons::parse(id).has_value();
}

// A copy of the seek buttons' argument rule (ForwardCommand/RewindCommand).
static int seekSeconds(const std::string &argument)
{
    int seconds = 10;
    std::from_chars(argument.data(), argument.data() + argument.size(), seconds);
    return std::clamp(seconds, 1, 600);
}

int main()
{
    // the four ids the row carries
    check(parsesTo("rewind:10", "rewind", "10"), "rewind:10 -> {rewind, 10}");
    check(parsesTo("playpause", "playpause", ""), "playpause -> {playpause, \"\"}");
    check(parsesTo("forward:10", "forward", "10"), "forward:10 -> {forward, 10}");
    check(parsesTo("skip", "skip", ""), "skip -> {skip, \"\"}");

    // ids no button of ours carries
    check(unknown(""), "\"\" -> unknown");
    check(unknown("garbage"), "garbage -> unknown");
    check(unknown("garbage:10"), "garbage:10 -> unknown");
    check(unknown("stop"), "stop (a command without a button) -> unknown");
    check(unknown(":10"), ":10 -> unknown");
    check(unknown("Forward:10"), "Forward:10 -> unknown (names are case-sensitive)");

    // the argument is the command's to read
    check(parsesTo("forward:", "forward", ""), "forward: -> {forward, \"\"}");
    check(parsesTo("forward:abc", "forward", "abc"), "forward:abc -> {forward, abc}");
    check(seekSeconds("10") == 10, "  seek rule: 10 -> 10 s");
    check(seekSeconds("") == 10 && seekSeconds("abc") == 10, "  seek rule: empty or garbage -> the default 10 s");
    check(seekSeconds("99999999999") == 10, "  seek rule: a number too big for int -> the default 10 s");
    check(seekSeconds("0") == 1 && seekSeconds("-5") == 1 && seekSeconds("700") == 600,
          "  seek rule: clamped to 1..600 like the slash path");

    // controlRow and parse agree, and the row is what Discord gets
    const dpp::component row = buttons::controlRow();
    check(row.type == dpp::cot_action_row && row.components.size() == 4, "controlRow is one action row of four buttons");

    struct Expected { const char *id; const char *emoji; const char *label; dpp::component_style style; };
    const Expected expected[] = {
        {"rewind:10", "\xE2\x8F\xAA", "10s", dpp::cos_secondary},
        {"playpause", "\xE2\x8F\xAF\xEF\xB8\x8F", "", dpp::cos_primary},
        {"forward:10", "\xE2\x8F\xA9", "10s", dpp::cos_secondary},
        {"skip", "\xE2\x8F\xAD\xEF\xB8\x8F", "Skip", dpp::cos_secondary},
    };
    for (size_t i = 0; i < row.components.size() && i < 4; ++i) {
        const dpp::component &button = row.components[i];
        const Expected &want = expected[i];
        const std::string what = std::string("button ") + want.id;
        check(button.type == dpp::cot_button && button.custom_id == want.id, (what + ": a button with that id").c_str());
        check(button.emoji.name == want.emoji && button.emoji.id == 0, (what + ": Discord's emoji bytes").c_str());
        check(button.label == want.label && button.style == want.style, (what + ": label and style").c_str());
        check(buttons::parse(button.custom_id).has_value(), (what + ": parse knows it").c_str());
    }

    dpp::message message("Playing: x");
    message.add_component(row);
    const dpp::json sent = dpp::json::parse(message.build_json());
    const dpp::json &sentButtons = sent["components"][0]["components"];
    check(!message.is_using_components_v2(), "the row keeps the message on components v1");
    check(sent["components"][0]["type"] == 1 && sentButtons.size() == 4, "the json carries one row of four");
    check(!sentButtons[1].contains("label") && sentButtons[1]["emoji"]["name"] == "\xE2\x8F\xAF\xEF\xB8\x8F",
          "the play/pause button goes out emoji-only");
    std::printf("\ncomponents as sent: %s\n\n", sent["components"].dump().c_str());

    return summary();
}
