#pragma once

// Tells a broken song from a broken network. Pure bookkeeping.
class FailureStreak
{
public:
    struct Decision
    {
        bool retry{false};         // keep the song and try it again
        bool firstOfStreak{false}; // this streak just turned into a hold: say so once
    };

    FailureStreak(int holdFromFailure_, int maxRetriesPerSong_);

    // One failed song; retriesSoFar is how often this very song was retried.
    Decision recordFailure(int retriesSoFar);

    // A song played, was skipped, or the queue ran empty.
    void reset();

    int length() const; // for the log

private:
    const int holdFromFailure;
    const int maxRetriesPerSong;
    int streak{0};
    bool announced{false};
};
