#pragma once

#include <cstdint>

namespace dpp {
struct interaction_create_t;
}

namespace interactions {

// Discord drops an interaction 3 s after it was issued, so how much of that
// budget was already gone on arrival tells a late dispatch from a slow handler.
int64_t ageMs(const dpp::interaction_create_t &event);

}
