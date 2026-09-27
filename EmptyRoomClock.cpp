#include "EmptyRoomClock.h"

EmptyRoomClock::EmptyRoomClock(int64_t leaveAfterSeconds_)
    : leaveAfterSeconds(leaveAfterSeconds_)
{
}

EmptyRoomClock::Verdict EmptyRoomClock::observe(Channel channel, int64_t nowSeconds)
{
    if (channel == Channel::NotInVoice) {
        emptySinceSeconds.reset();
        return Verdict::Stay;
    }

    if (channel == Channel::Occupied) {
        const bool wasRunning = emptySinceSeconds.has_value();
        emptySinceSeconds.reset();
        return wasRunning ? Verdict::ClockStopped : Verdict::Stay;
    }

    if (!emptySinceSeconds) {
        emptySinceSeconds = nowSeconds;
        return Verdict::ClockStarted;
    }

    if (nowSeconds - *emptySinceSeconds < leaveAfterSeconds) {
        return Verdict::Stay;
    }

    emptySinceSeconds.reset();
    return Verdict::Leave;
}
