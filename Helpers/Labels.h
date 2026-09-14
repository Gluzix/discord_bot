#pragma once

#include <string>

// How songs are shown in replies and in /queue.
namespace labels {

// Queue entries hold the raw yt-dlp target; show searches in a friendlier
// form until the real title is resolved. <> around a bare url stops Discord
// from unfurling an embed preview for it.
std::string displayFor(const std::string &target);

// A resolved song renders as a masked link - the title as clickable text,
// <> suppressing the embed preview. Unresolved songs fall back to the target.
std::string render(const std::string &title, const std::string &webpageUrl, const std::string &fallbackTarget);

}
