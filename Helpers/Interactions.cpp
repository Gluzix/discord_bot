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

}
