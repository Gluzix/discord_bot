#include "TimeText.h"

#include <cstdint>
#include <vector>

namespace {

const int64_t MAX_POSITION_SECONDS = 359999; // 99:59:59, well past any song

std::string twoDigits(int64_t value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

}

namespace timetext {

std::optional<int> parsePosition(const std::string &text)
{
    std::vector<int64_t> fields;
    size_t start = 0;
    for (;;) {
        const size_t colon = text.find(':', start);
        const size_t end = colon == std::string::npos ? text.size() : colon;
        if (end == start || end - start > 6) {
            return std::nullopt;
        }
        int64_t value = 0;
        for (size_t i = start; i < end; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return std::nullopt;
            }
            value = value * 10 + (text[i] - '0');
        }
        fields.push_back(value);
        if (colon == std::string::npos) {
            break;
        }
        start = colon + 1;
    }

    if (fields.size() > 3) {
        return std::nullopt;
    }
    for (size_t i = 1; i < fields.size(); ++i) {
        if (fields[i] > 59) {
            return std::nullopt;
        }
    }

    int64_t seconds = 0;
    for (int64_t field : fields) {
        seconds = seconds * 60 + field;
    }
    if (seconds > MAX_POSITION_SECONDS) {
        return std::nullopt;
    }
    return static_cast<int>(seconds);
}

std::string formatPosition(double seconds)
{
    const int64_t total = seconds > 0 ? static_cast<int64_t>(seconds + 0.5) : 0;
    const int64_t hours = total / 3600;
    const int64_t minutes = (total % 3600) / 60;
    if (hours > 0) {
        return std::to_string(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(total % 60);
    }
    return std::to_string(minutes) + ":" + twoDigits(total % 60);
}

std::string formatProgress(double positionSeconds, double durationSeconds)
{
    std::string text = formatPosition(positionSeconds);
    if (durationSeconds > 0) {
        text += " / " + formatPosition(durationSeconds);
    }
    return text;
}

}
