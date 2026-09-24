#include "VoiceDrainWatchdog.h"
#include "Check.h"

using Clock = VoiceDrainWatchdog::Clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

int main()
{
    const milliseconds deadAfter(10000);
    const Clock::time_point start{};

    {
        VoiceDrainWatchdog watchdog(deadAfter);
        bool fired = false;
        float remaining = 60.0f;
        for (int i = 0; i < 600; ++i) { // 60 s of a healthy client
            remaining -= 0.05f;
            fired |= watchdog.observe(remaining, false, start + milliseconds(100 * i));
        }
        check(!fired, "shrinking buffer never fires");
    }

    {
        VoiceDrainWatchdog watchdog(deadAfter);
        bool firedEarly = false;
        for (int i = 0; i < 100; ++i) { // 0 .. 9.9 s
            firedEarly |= watchdog.observe(1.0f, false, start + milliseconds(100 * i));
        }
        check(!firedEarly, "constant buffer silent before 10 s");
        check(watchdog.observe(1.0f, false, start + seconds(10)), "constant buffer fires at 10 s");
    }

    {
        VoiceDrainWatchdog watchdog(deadAfter);
        bool fired = false;
        for (int i = 0; i < 600; ++i) { // a 60 s pause
            fired |= watchdog.observe(1.0f, true, start + milliseconds(100 * i));
        }
        check(!fired, "paused never fires, however long");
        check(!watchdog.observe(1.0f, false, start + seconds(60)), "unpausing does not fire at once");
        check(watchdog.observe(1.0f, false, start + seconds(70)), "still stuck after a pause fires");
    }

    {
        VoiceDrainWatchdog watchdog(deadAfter);
        watchdog.observe(1.0f, false, start);
        watchdog.observe(1.0f, false, start + seconds(9));
        watchdog.reset();
        check(!watchdog.observe(1.0f, false, start + seconds(10)), "reset restarts the clock");
        check(!watchdog.observe(1.0f, false, start + seconds(19)), "silent 9 s after the reset");
        check(watchdog.observe(1.0f, false, start + seconds(20)), "fires 10 s after the reset");
    }

    return summary();
}
