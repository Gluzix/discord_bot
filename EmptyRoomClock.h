#pragma once

#include <cstdint>
#include <optional>

// Says when the bot has been alone in its voice channel for too long.
// Pure bookkeeping.
// =======================================================
// Rules:
// - observe() counts whole seconds, as dpp's timer ticks on: on a finer
//   clock the look at five minutes can read a moment short of them, and
//   the bot leaves one look late.
// =======================================================
class EmptyRoomClock
{
public:
    enum class Channel {
        NotInVoice,
        Empty,    // nobody but the bot
        Occupied,
    };

    enum class Verdict {
        Stay,         // nothing to say
        ClockStarted, // the channel went empty
        ClockStopped, // someone is there again
        Leave,        // empty for leaveAfterSeconds
    };

    explicit EmptyRoomClock(int64_t leaveAfterSeconds_);

    Verdict observe(Channel channel, int64_t nowSeconds);

private:
    const int64_t leaveAfterSeconds;
    std::optional<int64_t> emptySinceSeconds;
};
