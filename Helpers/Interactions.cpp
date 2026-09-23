#include "Interactions.h"

#include <dpp/dpp.h>

#include <chrono>

namespace interactions {

int64_t ageMs(const dpp::interaction_create_t &event)
{
    const std::chrono::duration<double> issuedAt(event.command.id.get_creation_time());
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch() - issuedAt).count();
}

void reply(const dpp::interaction_create_t &event, const std::string &text)
{
    dpp::message msg(text);
    // Every component interaction, not only buttons - fine while buttons are
    // the only components this bot sends.
    if (event.command.type == dpp::it_component_button) {
        msg.set_flags(dpp::m_ephemeral);
    }
    event.reply(msg);
}

}
