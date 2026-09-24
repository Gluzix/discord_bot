#include "FailureStreak.h"
#include "Check.h"

using Decision = FailureStreak::Decision;

int main()
{
    const int HOLD_FROM = 2;  // as PlaybackController builds it
    const int MAX_RETRIES = 5;

    { // one broken song among healthy ones
        FailureStreak streak(HOLD_FROM, MAX_RETRIES);
        const Decision first = streak.recordFailure(0);
        check(!first.retry && !first.firstOfStreak, "one failure drops the song, quietly");
        check(streak.length() == 1, "length counts the first failure");
    }

    { // the network went away
        FailureStreak streak(HOLD_FROM, MAX_RETRIES);
        streak.recordFailure(0);
        const Decision second = streak.recordFailure(0);
        check(second.retry && second.firstOfStreak, "second failure in a row holds and announces");
        const Decision third = streak.recordFailure(1);
        check(third.retry && !third.firstOfStreak, "the same song held again, no second notice");
        check(streak.length() == 3, "length counts every failure of the streak");
    }

    { // a held song runs out of retries, the streak goes on
        FailureStreak streak(HOLD_FROM, MAX_RETRIES);
        streak.recordFailure(0);
        bool allHeld = true;
        bool announcedTwice = false;
        for (int retriesSoFar = 0; retriesSoFar < MAX_RETRIES; ++retriesSoFar) {
            const Decision held = streak.recordFailure(retriesSoFar);
            allHeld = allHeld && held.retry;
            announcedTwice = announcedTwice || (held.firstOfStreak && retriesSoFar > 0);
        }
        check(allHeld, "every attempt up to the maximum is held");
        check(!announcedTwice, "one notice per streak, not per retry");

        const Decision spent = streak.recordFailure(MAX_RETRIES);
        check(!spent.retry && !spent.firstOfStreak, "the song is dropped once its retries are spent");
        check(streak.length() == MAX_RETRIES + 2, "the streak survives the drop");

        const Decision nextSong = streak.recordFailure(0);
        check(nextSong.retry && !nextSong.firstOfStreak, "the next song is held at once, quietly");
    }

    { // a song played, or /stop, or the queue ran empty
        FailureStreak streak(HOLD_FROM, MAX_RETRIES);
        streak.recordFailure(0);
        streak.recordFailure(0);
        streak.reset();
        check(streak.length() == 0, "reset clears the streak");
        const Decision lone = streak.recordFailure(0);
        check(!lone.retry, "after a reset one failure is a plain drop again");
        const Decision again = streak.recordFailure(0);
        check(again.retry && again.firstOfStreak, "a new streak announces again");
    }

    return summary();
}
