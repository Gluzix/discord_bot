#include "QueueEdit.h"
#include "Check.h"

#include <dpp/dpp.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

using queueedit::RemoveOutcome;
using Ids = std::vector<uint64_t>;

enum class Front { Plain, Held, LoopReplay, NotStarted };

// Songs 1..count; song 1 is the front, of the given kind.
static std::deque<Song> queueOf(uint64_t count, Front front = Front::Plain)
{
    std::deque<Song> queue;
    for (uint64_t id = 1; id <= count; ++id) {
        Song song;
        song.id = id;
        song.wasQueued = true; // the default, false, pins the song
        queue.push_back(std::move(song));
    }
    if (!queue.empty()) {
        queue.front().failedAttempts = front == Front::Held ? 1 : 0;
        queue.front().isLoopReplay = front == Front::LoopReplay;
        queue.front().wasQueued = front != Front::NotStarted;
    }
    return queue;
}

static Ids idsOf(const std::deque<Song> &queue)
{
    Ids ids;
    for (const Song &song : queue) {
        ids.push_back(song.id);
    }
    return ids;
}

int main()
{
    { // pinnedFront
        check(queueedit::pinnedFront(queueOf(0)) == 0, "pinnedFront: an empty queue pins nothing");
        check(queueedit::pinnedFront(queueOf(3)) == 0, "pinnedFront: a plain front is a waiting song");
        check(queueedit::pinnedFront(queueOf(3, Front::Held)) == 1, "pinnedFront: a held front is pinned");
        check(queueedit::pinnedFront(queueOf(3, Front::LoopReplay)) == 1, "pinnedFront: a loop replay at the front is pinned");
        check(queueedit::pinnedFront(queueOf(3, Front::NotStarted)) == 1, "pinnedFront: a front not started yet is pinned");
        std::deque<Song> heldBehind = queueOf(3);
        heldBehind[1].failedAttempts = 1;
        check(queueedit::pinnedFront(heldBehind) == 0, "pinnedFront: a held song behind the front pins nothing");
    }

    { // ten plain songs
        std::mt19937 rng(42);
        std::deque<Song> queue = queueOf(10);
        const size_t count = queueedit::shuffle(queue, rng);
        Ids sorted = idsOf(queue);
        std::sort(sorted.begin(), sorted.end());
        check(count == 10, "shuffle: all ten songs take part");
        check(sorted == idsOf(queueOf(10)), "shuffle: the same ten songs come back, each once");
        check(idsOf(queue) != idsOf(queueOf(10)), "shuffle: in another order");
    }

    { // the same seed
        std::mt19937 firstRng(42);
        std::mt19937 secondRng(42);
        std::deque<Song> first = queueOf(10);
        std::deque<Song> second = queueOf(10);
        queueedit::shuffle(first, firstRng);
        queueedit::shuffle(second, secondRng);
        check(idsOf(first) == idsOf(second), "shuffle: the same seed gives the same order");
    }

    { // a pinned front
        std::mt19937 rng(42);
        std::deque<Song> queue = queueOf(10, Front::Held);
        const size_t count = queueedit::shuffle(queue, rng);
        check(count == 9 && queue.front().id == 1, "shuffle: a pinned front stays first, the nine behind take part");
    }

    { // too few to shuffle
        std::mt19937 rng(42);
        std::deque<Song> one = queueOf(1);
        std::deque<Song> none;
        check(queueedit::shuffle(one, rng) == 1 && idsOf(one) == Ids{1}, "shuffle: one song is left alone, count 1");
        check(queueedit::shuffle(none, rng) == 0 && none.empty(), "shuffle: no song, count 0");
    }

    { // two behind a pinned front
        std::mt19937 rng(42);
        std::deque<Song> queue = queueOf(3, Front::NotStarted);
        const size_t count = queueedit::shuffle(queue, rng);
        Ids behind = {queue[1].id, queue[2].id};
        std::sort(behind.begin(), behind.end());
        check(count == 2 && queue.front().id == 1, "shuffle: two behind a pinned front, count 2, the front first");
        check(behind == Ids{2, 3}, "shuffle: the same two songs behind it");
    }

    { // positions the queue does not have
        std::deque<Song> queue = queueOf(3);
        const queueedit::Removal zero = queueedit::remove(queue, 0);
        const queueedit::Removal past = queueedit::remove(queue, 4);
        check(zero.outcome == RemoveOutcome::NoSuchPosition && !zero.song, "remove: position 0 is no such position");
        check(past.outcome == RemoveOutcome::NoSuchPosition && !past.song, "remove: a position past the end is no such position");
        check(idsOf(queue) == Ids{1, 2, 3}, "remove: neither changes the queue");
    }

    { // a song in the middle
        std::deque<Song> queue = queueOf(5);
        const queueedit::Removal removal = queueedit::remove(queue, 3);
        check(removal.outcome == RemoveOutcome::Removed && removal.song && removal.song->id == 3,
              "remove: position 3 takes song 3 out");
        check(idsOf(queue) == Ids{1, 2, 4, 5}, "remove: the others keep their order");
    }

    { // the plain front
        std::deque<Song> queue = queueOf(3);
        const queueedit::Removal removal = queueedit::remove(queue, 1);
        check(removal.outcome == RemoveOutcome::Removed && removal.song && removal.song->id == 1
              && idsOf(queue) == Ids{2, 3}, "remove: position 1 of a plain queue takes song 1 out");
    }

    { // a pinned front
        struct Case { Front front; RemoveOutcome outcome; const char *what; };
        const Case cases[] = {
            {Front::Held, RemoveOutcome::Held, "remove: position 1, held for a retry, is Held"},
            {Front::LoopReplay, RemoveOutcome::UpNext, "remove: position 1, a loop replay, is UpNext"},
            {Front::NotStarted, RemoveOutcome::UpNext, "remove: position 1, not started yet, is UpNext"},
        };
        for (const Case &pinned : cases) {
            std::deque<Song> queue = queueOf(3, pinned.front);
            const queueedit::Removal removal = queueedit::remove(queue, 1);
            check(removal.outcome == pinned.outcome && !removal.song && idsOf(queue) == Ids{1, 2, 3}, pinned.what);
        }
    }

    { // behind a pinned front
        std::deque<Song> queue = queueOf(3, Front::Held);
        const queueedit::Removal removal = queueedit::remove(queue, 2);
        check(removal.outcome == RemoveOutcome::Removed && removal.song && removal.song->id == 2
              && idsOf(queue) == Ids{1, 3}, "remove: position 2 behind a pinned front takes song 2 out");
    }

    return summary();
}
