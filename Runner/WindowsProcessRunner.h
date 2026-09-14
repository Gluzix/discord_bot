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
};

class WindowsProcessRunner
{
public:
    // Returns true once the caller no longer wants the result; yt-dlp is
    // then killed within a poll interval. Empty means never.
    using CancelCheck = std::function<bool()>;

    // Runs yt-dlp once to resolve both the video title and the direct media
    // URL. The target is either a YouTube url or a "ytsearch1:<query>"
    // search expression. directUrl is empty on failure or cancellation.
    static ResolvedMedia resolveMedia(const std::string &target, const CancelCheck &cancelled = {});

    // Lists the first maxEntries videos of a YouTube playlist without
    // resolving any of them - one quick yt-dlp run. Private and deleted
    // videos are left out.
    static PlaylistListing listPlaylist(const std::string &playlistUrl, size_t maxEntries);

private:
    struct YtDlpOutput
    {
        unsigned long exitCode{1};
        bool cancelled{false};          // killed because the caller lost interest
        std::vector<std::string> lines; // stdout split into non-empty lines
    };
    // Every run is bounded: the CancelCheck or YT_DLP_TIMEOUT_SECONDS kills
    // yt-dlp together with everything it spawned.
    static YtDlpOutput runYtDlp(const std::string &args, const CancelCheck &cancelled);

    static const int PIPE_READ_CHUNK = 4096;
    static const int PIPE_BUFFER_BYTES = 64 * 1024;
    static const int POLL_INTERVAL_MS = 50;
    static const int YT_DLP_TIMEOUT_SECONDS = 60;
    static const std::string YT_DLP;
    static const std::string YT_DLP_PATH;
    static const std::string YT_DLP_SONG_ARGS;
};

