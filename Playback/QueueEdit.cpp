#include "QueueEdit.h"

#include <dpp/dpp.h>

#include <algorithm>

namespace queueedit {

size_t pinnedFront(const std::deque<Song> &queue)
{
    if (queue.empty()) {
        return 0;
    }
    const Song &front = queue.front();
    const bool pinned = front.failedAttempts > 0 || front.isLoopReplay || !front.wasQueued;
    return pinned ? 1 : 0;
}

size_t shuffle(std::deque<Song> &queue, std::mt19937 &rng)
{
    const size_t pinned = pinnedFront(queue);
    const size_t waiting = queue.size() - pinned;
    if (waiting >= 2) {
        std::shuffle(queue.begin() + pinned, queue.end(), rng);
    }
    return waiting;
}

Removal remove(std::deque<Song> &queue, size_t position)
{
    if (position == 0 || position > queue.size()) {
        return {RemoveOutcome::NoSuchPosition};
    }
    if (position <= pinnedFront(queue)) {
        const bool held = queue[position - 1].failedAttempts > 0;
        return {held ? RemoveOutcome::Held : RemoveOutcome::UpNext};
    }

    const auto at = queue.begin() + (position - 1);
    Removal removal{RemoveOutcome::Removed, std::move(*at)};
    queue.erase(at);
    return removal;
}

}
