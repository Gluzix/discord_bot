#pragma once

#include <string>

// Checks what users type before it reaches yt-dlp.
// =======================================================
// Rules:
// - User input ends up on a yt-dlp command line: these checks keep it to
//   plain YouTube links and tame search text, so nothing can break out of
//   the quoted argument or inject extra arguments.
// =======================================================
namespace youtube {

bool isAllowedUrl(const std::string &url);

// Free text stays short and free of quotes/control characters.
bool isReasonableSearchQuery(const std::string &query);

// Any allowed link that names a playlist - a watch?v=...&list=... link too.
bool isPlaylistUrl(const std::string &url);

// A playlist page with no video of its own; /play has nothing to play there.
bool isPlaylistPageUrl(const std::string &url);

}
