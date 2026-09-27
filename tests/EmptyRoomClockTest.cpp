#include "EmptyRoomClock.h"
#include "Check.h"

using Channel = EmptyRoomClock::Channel;
using Verdict = EmptyRoomClock::Verdict;

int main()
{
    const int64_t LEAVE_AFTER_SECONDS = 300;

    { // people listening
        EmptyRoomClock clock(LEAVE_AFTER_SECONDS);
        bool silent = true;
        for (int64_t now = 0; now <= 3 * LEAVE_AFTER_SECONDS; now += 30) {
            silent = silent && clock.observe(Channel::Occupied, now) == Verdict::Stay;
        }
        check(silent, "occupied looks never say anything");
    }

    { // everybody went
        EmptyRoomClock clock(LEAVE_AFTER_SECONDS);
        check(clock.observe(Channel::Empty, 0) == Verdict::ClockStarted, "the first empty look starts the clock");
        bool stayed = true;
        for (int64_t now = 1; now < LEAVE_AFTER_SECONDS; ++now) {
            stayed = stayed && clock.observe(Channel::Empty, now) == Verdict::Stay;
        }
        check(stayed, "empty up to 299 s stays");
        check(clock.observe(Channel::Empty, LEAVE_AFTER_SECONDS) == Verdict::Leave, "empty for 300 s leaves");
        check(clock.observe(Channel::Empty, LEAVE_AFTER_SECONDS + 30) == Verdict::ClockStarted,
              "after a leave an empty look starts the clock again");
    }

    { // someone came back in time
        EmptyRoomClock clock(LEAVE_AFTER_SECONDS);
        clock.observe(Channel::Empty, 0);
        check(clock.observe(Channel::Occupied, 299) == Verdict::ClockStopped, "someone at 299 s stops the clock");
        check(clock.observe(Channel::Occupied, 329) == Verdict::Stay, "a second occupied look stays quiet");
        check(clock.observe(Channel::Empty, 359) == Verdict::ClockStarted, "the next empty look starts a new clock");
        check(clock.observe(Channel::Empty, 359 + 299) == Verdict::Stay, "the new clock counts from its own start");
        check(clock.observe(Channel::Empty, 359 + 300) == Verdict::Leave, "and leaves 300 s after it");
    }

    { // the bot left voice, or a rejoin is under way
        EmptyRoomClock clock(LEAVE_AFTER_SECONDS);
        clock.observe(Channel::Empty, 0);
        check(clock.observe(Channel::NotInVoice, 30) == Verdict::Stay, "out of voice while the clock runs stays quiet");
        check(clock.observe(Channel::Occupied, 60) == Verdict::Stay, "the next occupied look is no ClockStopped");
        check(clock.observe(Channel::Empty, 90) == Verdict::ClockStarted, "the next empty look starts the clock");
    }

    return summary();
}
