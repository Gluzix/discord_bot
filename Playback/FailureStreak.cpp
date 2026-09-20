#include "FailureStreak.h"

FailureStreak::FailureStreak(int holdFromFailure_, int maxRetriesPerSong_)
    : holdFromFailure(holdFromFailure_)
    , maxRetriesPerSong(maxRetriesPerSong_)
{
}

FailureStreak::Decision FailureStreak::recordFailure(int retriesSoFar)
{
    ++streak;

    Decision decision;
    decision.retry = streak >= holdFromFailure && retriesSoFar < maxRetriesPerSong;
    if (decision.retry && !announced) {
        announced = true;
        decision.firstOfStreak = true;
    }
    return decision;
}

void FailureStreak::reset()
{
    streak = 0;
    announced = false;
}

int FailureStreak::length() const
{
    return streak;
}
