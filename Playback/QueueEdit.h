#pragma once

#include "Song.h"

#include <deque>
#include <optional>
#include <random>

// What /shuffle and /remove do to the waiting songs.
// =======================================================
// Rules:
// - A front song that is held for a retry, that replays a looping song,
//   or that was asked for while nothing played and has not started yet,
//   is the current song waiting for its turn: pinnedFront() counts it,
//   so shuffle() leaves it in front and remove() will not take it. Moved,
//   a held song breaks the hold, which looks for it at the front; a loop
//   replay plays later without its announcement; and the third kind
//   announces by editing its own /play reply, which by then is far up
//   the channel or too old to edit.
// =======================================================
namespace queueedit {

// 1 when the front song must stay where it is, else 0.
size_t pinnedFront(const std::deque<Song> &queue);

// Shuffles the songs behind the pinned front and returns how many
// those are; fewer than two are left as they are.
size_t shuffle(std::deque<Song> &queue, std::mt19937 &rng);

enum class RemoveOutcome {
    Removed,
    NoSuchPosition,
    Held,           // the pinned front, held for a retry: /skip takes it
    UpNext,         // the pinned front, about to play
};

struct Removal
{
    RemoveOutcome outcome{RemoveOutcome::NoSuchPosition};
    std::optional<Song> song; // the song taken out, when Removed
};

// position counts the waiting songs from 1, as /queue numbers them.
Removal remove(std::deque<Song> &queue, size_t position);

}
