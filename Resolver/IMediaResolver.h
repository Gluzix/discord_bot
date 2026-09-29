#pragma once

#include <functional>
#include <string>
#include <vector>

struct ResolvedMedia
{
    std::string title;
    std::string webpageUrl; // canonical YouTube page (also for search results)
    std::string directUrl;  // empty on failure
};

struct PlaylistEntry
{
    std::string webpageUrl; // the video's own YouTube page - a valid /play target
    std::string title;
};

struct PlaylistListing
{
    std::string title;                  // the playlist's own name (empty if unknown)
    std::vector<PlaylistEntry> entries; // playable videos in playlist order; empty on failure
    size_t totalCount{0};               // how many videos the playlist has in all (0 = unknown)
    bool timedOut{false};               // ran out of time; entries is empty then
};

// Turns what a user asked for into something that can be played.
// =======================================================
// Rules:
// - One resolver serves the resolver thread, the decoder thread and the
//   commands at once: resolveMedia() and listPlaylist() must be safe to
//   call concurrently.
// =======================================================
class IMediaResolver
{
public:
    // Returns true once the caller no longer wants the result;
    // resolveMedia() then returns within a poll interval. Empty means
    // never.
    using CancelCheck = std::function<bool()>;

    virtual ~IMediaResolver() = default;

    // Resolves the video title, page url and direct media URL. The target
    // is either a YouTube url or a "ytsearch1:<query>" search expression;
    // a search resolves to its first plain-video result. directUrl is
    // empty on failure or cancellation.
    virtual ResolvedMedia resolveMedia(const std::string &target, const CancelCheck &cancelled) = 0;

    // Lists the first maxEntries videos of a YouTube playlist without
    // resolving any of them. Private and deleted videos are left out, and
    // so are the repeats of a mix.
    // A listing that timed out comes back empty, with timedOut set.
    virtual PlaylistListing listPlaylist(const std::string &playlistUrl, size_t maxEntries) = 0;
};
