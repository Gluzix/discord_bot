#pragma once

#include <optional>
#include <string>

// Song positions as users type them and read them.
namespace timetext {

// Accepts `90`, `1:30` and `1:02:03`; nullopt for anything else. Fields below
// the largest unit must stay under 60, and no field may be empty.
std::optional<int> parsePosition(const std::string &text);

// `m:ss`, or `h:mm:ss` from an hour on.
std::string formatPosition(double seconds);

// `2:35 / 4:10`, or just the position when the duration is unknown (0).
std::string formatProgress(double positionSeconds, double durationSeconds);

}
