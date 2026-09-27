#pragma once

#include "IMediaResolver.h"
#include "IProcessRunner.h"

#include <memory>
#include <string>
#include <vector>

// Builds yt-dlp's command lines and reads its answers.
// =======================================================
// Rules:
// - yt-dlp writes piped output in the ANSI code page (cp1250 here) without
//   --encoding utf-8: every runYtDlp() caller passes the flag.
// - A band-name search often ranks the artist's channel first, and handing
//   that to "ytsearch1:" makes yt-dlp extract every upload on it (minutes,
//   with the title of the first and the url of the last) - so a search picks
//   the first plain video from a flat listing instead (resolveMedia(),
//   firstVideoUrl()).
// =======================================================
class YtDlpResolver : public IMediaResolver
{
public:
    // program is the yt-dlp to run; the bot passes programFromEnvironment().
    YtDlpResolver(std::shared_ptr<IProcessRunner> runner_, std::string program_);

    // YT_DLP_PATH when set and not empty, else "yt-dlp" from PATH.
    static std::string programFromEnvironment();

    ResolvedMedia resolveMedia(const std::string &target, const CancelCheck &cancelled) override;
    PlaylistListing listPlaylist(const std::string &playlistUrl, size_t maxEntries) override;

private:
    IProcessRunner::Output runYtDlp(const std::vector<std::string> &arguments, const CancelCheck &cancelled);

    // Lists the top few search results flat (no extraction, ~2s) and returns
    // the first plain video's url; empty when there is none.
    std::string firstVideoUrl(const std::string &query, const CancelCheck &cancelled);

    std::shared_ptr<IProcessRunner> runner;
    std::string program;

    static const int YT_DLP_TIMEOUT_SECONDS = 60;
    static const std::vector<std::string> YT_DLP_SONG_ARGS;
    static const std::vector<std::string> YT_DLP_SEARCH_ARGS;
    static const std::string SEARCH_PREFIX;
};
