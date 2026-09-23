#include "PlaybackButtons.h"

#include <dpp/dpp.h>
#include <dpp/unicode_emoji.h>

namespace {

dpp::component button(const char *id, const char *emoji, const char *label, dpp::component_style style)
{
    return dpp::component().set_label(label).set_emoji(emoji).set_style(style).set_id(id);
}

}

namespace buttons {

dpp::component controlRow()
{
    return dpp::component()
        .set_type(dpp::cot_action_row)
        .add_component(button("rewind:10", dpp::unicode_emoji::rewind, "10s", dpp::cos_secondary))
        .add_component(button(PLAY_PAUSE, dpp::unicode_emoji::play_pause, "", dpp::cos_primary))
        .add_component(button("forward:10", dpp::unicode_emoji::fast_forward, "10s", dpp::cos_secondary))
        .add_component(button("skip", dpp::unicode_emoji::track_next, "Skip", dpp::cos_secondary));
}

std::optional<Click> parse(const std::string &customId)
{
    const size_t colon = customId.find(':');
    Click click;
    click.command = customId.substr(0, colon);
    if (colon != std::string::npos) {
        click.argument = customId.substr(colon + 1);
    }
    if (click.command != "rewind" && click.command != PLAY_PAUSE
        && click.command != "forward" && click.command != "skip") {
        return std::nullopt;
    }
    return click;
}

}
