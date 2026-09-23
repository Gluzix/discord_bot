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

// Runs yt-dlp and parses its output.
// =======================================================
// Rules:
// - runYtDlp() puts every run in a job object: a kill must also reach what
//   yt-dlp spawns (the PyInstaller child interpreter, a JS runtime).
// - runYtDlp() bounds every run: the pipe is polled with PeekNamedPipe, never
//   read blocking, so a cancel or YT_DLP_TIMEOUT_SECONDS can end it at any
//   moment.
// - In runYtDlp(), the command line goes through CreateProcessW as UTF-16:
//   the A variant would mangle non-ASCII search queries through the ANSI code
//   page.
// - yt-dlp writes piped output in the ANSI code page (cp1250 here) without
//   --encoding utf-8, and sometimes even with it, which Discord shows as
//   mojibake: every runYtDlp() caller passes the flag, and resolveMedia() and
//   listPlaylist() put every title through ensureUtf8().
// - A band-name search often ranks the artist's channel first, and handing
//   that to "ytsearch1:" makes yt-dlp extract every upload on it (minutes,
//   with the title of the first and the url of the last) - so a search picks
//   the first plain video from a flat listing instead (resolveMedia(),
//   firstVideoUrl()).
// =======================================================
class WindowsProcessRunner
{
public:
    // Returns true once the caller no longer wants the result; yt-dlp is
    // then killed within a poll interval. Empty means never.
    using CancelCheck = std::function<bool()>;

    // Resolves the video title, page url and direct media URL. The target is
    // either a YouTube url or a "ytsearch1:<query>" search expression; a
    // search resolves to its first plain-video result. directUrl is empty
    // on failure or cancellation.
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
    static YtDlpOutput runYtDlp(const std::string &args, const CancelCheck &cancelled);

    // Lists the top few search results flat (no extraction, ~2s) and returns
    // the first plain video's url; empty when there is none.
    static std::string firstVideoUrl(const std::string &query, const CancelCheck &cancelled);

    static const int PIPE_READ_CHUNK = 4096;
    static const int PIPE_BUFFER_BYTES = 64 * 1024;
    static const int POLL_INTERVAL_MS = 50;
    static const int YT_DLP_TIMEOUT_SECONDS = 60;
    static const std::string YT_DLP_SONG_ARGS;
    static const std::string YT_DLP_SEARCH_ARGS;
    static const std::string SEARCH_PREFIX;
};

