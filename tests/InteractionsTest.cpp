#include "Interactions.h"
#include "Check.h"

#include <dpp/dpp.h>

#include <cstdio>
#include <string>

// With from_webhook set, dpp queues the response json instead of posting it.
static dpp::json answer(const dpp::interaction_create_t &event)
{
    return dpp::json::parse(event.get_queued_response());
}

static dpp::json button(int id, const char *customId, const char *label, int style, const char *emoji)
{
    dpp::json b = {{"type", 2}, {"id", id}, {"custom_id", customId}, {"style", style},
                   {"emoji", {{"name", emoji}, {"id", nullptr}}}};
    if (*label) {
        b["label"] = label;
    }
    return b;
}

// The "Playing:" message the way Discord puts it into a component interaction.
static dpp::message clickedMessage(const std::string &content)
{
    dpp::json j = {
        {"id", "1400000000000000001"}, {"channel_id", "1400000000000000002"}, {"type", 0},
        {"content", content}, {"flags", 0},
        {"components", dpp::json::array({
            {{"type", 1}, {"id", 1}, {"components", dpp::json::array({
                button(2, "rewind:10", "10s", 2, "\xE2\x8F\xAA"),
                button(3, "playpause", "", 1, "\xE2\x8F\xAF\xEF\xB8\x8F"),
                button(4, "forward:10", "10s", 2, "\xE2\x8F\xA9"),
                button(5, "skip", "Skip", 2, "\xE2\x8F\xAD\xEF\xB8\x8F"),
            })}},
        })},
    };
    return dpp::message().fill_from_json(&j, dpp::cache_policy::cpol_default);
}

static dpp::button_click_t click(const dpp::message &on)
{
    dpp::button_click_t event;
    event.from_webhook = true;
    event.command.type = dpp::it_component_button;
    event.command.msg = on;
    return event;
}

static dpp::slashcommand_t slash()
{
    dpp::slashcommand_t event;
    event.from_webhook = true;
    event.command.type = dpp::it_application_command;
    return event;
}

// A snowflake's top bits count milliseconds from 2015-01-01, Discord's epoch.
static dpp::snowflake idMadeAt(uint64_t unixSeconds)
{
    return dpp::snowflake((unixSeconds * 1000 - 1420070400000ull) << 22);
}

static bool isOurRow(const dpp::json &components)
{
    if (components.size() != 1 || components[0]["type"] != 1 || components[0]["components"].size() != 4) {
        return false;
    }
    const char *ids[] = {"rewind:10", "playpause", "forward:10", "skip"};
    const char *emoji[] = {"\xE2\x8F\xAA", "\xE2\x8F\xAF\xEF\xB8\x8F", "\xE2\x8F\xA9", "\xE2\x8F\xAD\xEF\xB8\x8F"};
    for (size_t i = 0; i < 4; ++i) {
        const dpp::json &b = components[0]["components"][i];
        if (b["type"] != 2 || b["custom_id"] != ids[i] || b["emoji"]["name"] != emoji[i]) {
            return false;
        }
    }
    return components[0]["components"][0]["label"] == "10s" && !components[0]["components"][1].contains("label")
        && components[0]["components"][1]["style"] == 1 && components[0]["components"][3]["label"] == "Skip";
}

int main()
{
    const std::string title = "Playing: [@everyone Song](<https://www.youtube.com/watch?v=x>)";

    { // the first click on a fresh "Playing:" message
        const dpp::button_click_t event = click(clickedMessage(title));
        check(!event.command.msg.components.empty(), "dpp parsed the clicked message's row");
        interactions::reply(event, "Forwarded to 1:30 / 3:45");
        const dpp::json sent = answer(event);
        check(sent["type"] == dpp::ir_update_message, "click result: UPDATE_MESSAGE (7), nothing new posted");
        check(sent["data"]["content"] == title + "\nForwarded to 1:30 / 3:45", "click result: the title, then the status line");
        check(isOurRow(sent["data"]["components"]), "click result: the clicked row goes back unchanged");
        check(sent["data"]["allowed_mentions"]["parse"].empty(), "click result: no mention is parsed");
        check((sent["data"]["flags"].get<int>() & dpp::m_ephemeral) == 0, "click result: not ephemeral");
        std::printf("\nfirst click sends: %s\n\n", sent.dump().c_str());
    }

    { // a later click replaces the status line
        const dpp::button_click_t event = click(clickedMessage(title + "\nForwarded to 1:30 / 3:45"));
        interactions::reply(event, "Pausing currently playing song");
        check(answer(event)["data"]["content"] == title + "\nPausing currently playing song",
              "second click: the status line is replaced, not appended");
    }

    { // a click on a /nowplaying reply gets its first line anew
        const std::string stale = "Now playing: **[Song](<https://www.youtube.com/watch?v=x>)** - 0:10 / 3:45";
        const std::string fresh = "Now playing: **[Song](<https://www.youtube.com/watch?v=x>)** - 1:30 / 3:45 (paused)";
        const dpp::button_click_t event = click(clickedMessage(stale + "\nForwarded to 0:10 / 3:45"));
        interactions::reply(event, "Pausing currently playing song", fresh);
        const dpp::json sent = answer(event);
        check(sent["type"] == dpp::ir_update_message, "fresh title: UPDATE_MESSAGE (7)");
        check(sent["data"]["content"] == fresh + "\nPausing currently playing song",
              "fresh title: it replaces the first line, the status line follows");
        check(isOurRow(sent["data"]["components"]), "fresh title: the clicked row goes back unchanged");
        check(sent["data"]["allowed_mentions"]["parse"].empty(), "fresh title: no mention is parsed");
    }

    { // an empty fresh title is none
        const dpp::button_click_t event = click(clickedMessage(title + "\nSkipped!"));
        interactions::reply(event, "Forwarded to 1:30 / 3:45", "");
        check(answer(event)["data"]["content"] == title + "\nForwarded to 1:30 / 3:45",
              "no fresh title: the clicked title stays");
    }

    { // the click came without its message
        const dpp::button_click_t event = click(dpp::message());
        interactions::reply(event, "Skipped!");
        const dpp::json sent = answer(event);
        check(sent["type"] == dpp::ir_update_message && sent["data"]["content"] == "Skipped!",
              "no message: UPDATE_MESSAGE with the status line alone");
        check(isOurRow(sent["data"]["components"]), "no message: controlRow is put back");
    }

    { // refusals stay with the clicker
        const dpp::button_click_t event = click(clickedMessage(title));
        interactions::refuse(event, "You need to be in my voice channel to control the music!");
        const dpp::json sent = answer(event);
        check(sent["type"] == dpp::ir_channel_message_with_source
              && (sent["data"]["flags"].get<int>() & dpp::m_ephemeral) != 0,
              "click refusal: a new ephemeral message");
        check(sent["data"]["components"].empty(), "click refusal: no buttons on it");
    }

    { // slash commands answer the channel as before
        const dpp::slashcommand_t event = slash();
        interactions::reply(event, "Forwarded to 1:30 / 3:45");
        dpp::json sent = answer(event);
        check(sent["type"] == dpp::ir_channel_message_with_source && sent["data"]["flags"] == 0
              && sent["data"]["content"] == "Forwarded to 1:30 / 3:45",
              "slash result: a public channel reply");
        interactions::reply(event, "Forwarded to 1:30 / 3:45", "Now playing: nothing");
        sent = answer(event);
        check(sent["data"]["content"] == "Forwarded to 1:30 / 3:45", "slash result: a fresh title is ignored");
        interactions::refuse(event, "You need to be in my voice channel to control the music!");
        sent = answer(event);
        check(sent["type"] == dpp::ir_channel_message_with_source && sent["data"]["flags"] == 0,
              "slash refusal: a public channel reply, as before");
    }

    { // the token of a reply
        const int64_t madeAt = 1790000000;
        dpp::slashcommand_t event = slash();
        event.command.id = idMadeAt(madeAt);
        check(event.command.id.get_creation_time() == madeAt, "token: the id carries the time the interaction was made");
        check(interactions::tokenAlive(event, madeAt + 14 * 60 - 1), "token: 13:59 after the interaction it can be edited");
        check(!interactions::tokenAlive(event, madeAt + 14 * 60), "token: 14:00 after, it is too old");
        event.command.id = 0;
        check(!interactions::tokenAlive(event, madeAt), "token: an interaction without an id is too old");
    }

    return summary();
}
