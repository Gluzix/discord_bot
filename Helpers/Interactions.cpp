#include "Interactions.h"
#include "PlaybackButtons.h"

#include <dpp/dpp.h>
#include <QDebug>

#include <chrono>

namespace {

bool isClick(const dpp::interaction_create_t &event)
{
    return event.command.type == dpp::it_component_button;
}

}

namespace interactions {

int64_t ageMs(const dpp::interaction_create_t &event)
{
    const std::chrono::duration<double> issuedAt(event.command.id.get_creation_time());
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch() - issuedAt).count();
}

void reply(const dpp::interaction_create_t &event, const std::string &text)
{
    if (!isClick(event)) {
        event.reply(dpp::message(text));
        return;
    }

    const dpp::message &clicked = event.command.msg;
    std::string content = text;
    if (clicked.content.empty()) {
        qWarning() << "A click came without its message - the status line replaces the title";
    } else {
        content = clicked.content.substr(0, clicked.content.find('\n')) + '\n' + text;
    }

    dpp::message update(content);
    update.components = clicked.components;
    if (update.components.empty()) {
        update.add_component(buttons::controlRow());
    }
    // The title is untrusted input, and it goes out again.
    update.set_allowed_mentions();
    event.reply(dpp::ir_update_message, update);
}

void refuse(const dpp::interaction_create_t &event, const std::string &text)
{
    dpp::message msg(text);
    if (isClick(event)) {
        msg.set_flags(dpp::m_ephemeral);
    }
    event.reply(msg);
}

}
