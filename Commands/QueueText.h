#pragma once

#include "PlaybackController.h"

#include <string>

// What /queue answers.
// =======================================================
// Rules:
// - Discord refuses a message of more than 2000 characters, and fifteen
//   songs with long titles or links are more: render() lists a song only
//   while the text, its "...and N more" line included, stays within 2000.
// - render() counts UTF-16 units, which are never fewer than the code
//   points Discord counts; bytes would leave out Cyrillic or Japanese
//   titles that fit.
// - render() writes the "Now playing:" line unmeasured: YouTube caps a
//   title at 100 characters, so one label is a few hundred at most.
// =======================================================
namespace queuetext {

// The playing song, then the waiting ones, as many as fit.
std::string render(const PlaybackController::QueueSnapshot &snapshot);

}
