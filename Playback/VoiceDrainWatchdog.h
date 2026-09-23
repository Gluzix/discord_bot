#pragma once

#include <chrono>

// Pure bookkeeping: says when a voice client's send buffer stopped draining.
class VoiceDrainWatchdog
{
public:
    using Clock = std::chrono::steady_clock;

    explicit VoiceDrainWatchdog(std::chrono::milliseconds deadAfter_);

    // Returns true once the buffer has not shrunk for deadAfter. A paused
    // client is not a dead one.
    bool observe(float secsRemaining, bool paused, Clock::time_point now = Clock::now());

    // A packet went out: draining is proven, start over.
    void reset();

private:
    const std::chrono::milliseconds deadAfter;
    bool primed{false};
    float lastRemaining{0.0f};
    Clock::time_point stuckSince;
};
