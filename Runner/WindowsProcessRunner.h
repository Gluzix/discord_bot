#pragma once

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
    // Runs yt-dlp once to resolve both the video title and the direct media
    // URL. The target is either a YouTube url or a "ytsearch1:<query>"
    // search expression. directUrl is empty on failure.
    static ResolvedMedia resolveMedia(const std::string &target);

    // Lists the first maxEntries videos of a YouTube playlist without
    // resolving any of them - one quick yt-dlp run. Private and deleted
    // videos are left out.
    static PlaylistListing listPlaylist(const std::string &playlistUrl, size_t maxEntries);

private:
    struct YtDlpOutput
    {
        unsigned long exitCode{1};
        std::vector<std::string> lines; // stdout split into non-empty lines
    };
    static YtDlpOutput runYtDlp(const std::string &args);

    static const int PIPE_READ_CHUNK = 4096;
    static const std::string YT_DLP;
    static const std::string YT_DLP_PATH;
    static const std::string YT_DLP_SONG_ARGS;
};

