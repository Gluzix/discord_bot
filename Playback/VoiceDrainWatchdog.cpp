#include "VoiceDrainWatchdog.h"

VoiceDrainWatchdog::VoiceDrainWatchdog(std::chrono::milliseconds deadAfter_)
    : deadAfter(deadAfter_)
{
}

bool VoiceDrainWatchdog::observe(float secsRemaining, bool paused, Clock::time_point now)
{
    if (!primed || paused) {
        primed = true;
        stuckSince = now;
        lastRemaining = secsRemaining;
        return false;
    }

    const float PROGRESS_EPSILON = 0.01f;
    if (secsRemaining < lastRemaining - PROGRESS_EPSILON) {
        stuckSince = now;
    }
    lastRemaining = secsRemaining;
    return now - stuckSince >= deadAfter;
}

void VoiceDrainWatchdog::reset()
{
    primed = false;
}
