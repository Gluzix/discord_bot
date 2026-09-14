#pragma once

#include <string>
#include <memory>

namespace dpp {
struct slashcommand_t;
}

struct Song
{
    uint64_t id{0};
    std::string target; // youtube url or "ytsearch1:<query>"
    bool wasQueued{false}; // waited in the queue vs started right away
    std::unique_ptr<dpp::slashcommand_t> event;

    // Filled by the resolver thread ahead of time; empty until then.
    std::string title;
    std::string webpageUrl;
    std::string directUrl;
    int64_t resolvedAtSeconds{0};
    bool resolveFailed{false}; // resolver gave up; SongPlayer retries itself
    bool resolveInFlight{false}; // resolver is on it right now; the worker waits rather than resolving it twice
    bool isLoopReplay{false};  // song-mode repeat: suppress the "Playing:" announcement
    bool fromPlaylist{false};  // shares the /playlist reply: no per-song queue edits
};
