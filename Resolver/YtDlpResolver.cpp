#include "YtDlpResolver.h"

#include <QDebug>
#include <QString>
#include <cstdlib>
#include <utility>

const std::vector<std::string> YtDlpResolver::YT_DLP_SONG_ARGS = {
    "--no-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8", "-f", "bestaudio",
    "--print", "title", "--print", "webpage_url", "--print", "urls",
};
const std::vector<std::string> YtDlpResolver::YT_DLP_SEARCH_ARGS = {
    "--flat-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8",
    "--print", "%(ie_key)s %(url)s",
};
const std::string YtDlpResolver::SEARCH_PREFIX = "ytsearch1:";

YtDlpResolver::YtDlpResolver(std::shared_ptr<IProcessRunner> runner_, std::string program_)
    : runner(std::move(runner_))
    , program(std::move(program_))
{
}

std::string YtDlpResolver::programFromEnvironment()
{
    const QString path = qEnvironmentVariable("YT_DLP_PATH");
    return path.isEmpty() ? std::string("yt-dlp") : path.toStdString();
}

IProcessRunner::Output YtDlpResolver::runYtDlp(const std::vector<std::string> &arguments, const CancelCheck &cancelled)
{
    IProcessRunner::Output output = runner->run(program, arguments, std::chrono::seconds(YT_DLP_TIMEOUT_SECONDS), cancelled);
    if (!output.started) {
        qDebug() << "Could not start yt-dlp - put it on PATH or set YT_DLP_PATH";
    }
    return output;
}

std::string YtDlpResolver::firstVideoUrl(const std::string &query, const CancelCheck &cancelled)
{
    std::vector<std::string> arguments = YT_DLP_SEARCH_ARGS;
    arguments.push_back("ytsearch5:" + query);
    IProcessRunner::Output run = runYtDlp(arguments, cancelled);

    // One "<extractor> <url>" line per result: plain videos come from
    // "Youtube", channels and playlists from "YoutubeTab".
    const std::string videoMarker = "Youtube ";
    for (const std::string &line : run.lines) {
        if (line.rfind(videoMarker, 0) == 0) {
            return line.substr(videoMarker.size());
        }
    }
    if (!run.cancelled) {
        qDebug() << "yt-dlp search found no video, exit code:" << run.exitCode;
    }
    return {};
}

ResolvedMedia YtDlpResolver::resolveMedia(const std::string &target, const CancelCheck &cancelled)
{
    std::string url = target;
    if (target.rfind(SEARCH_PREFIX, 0) == 0) {
        url = firstVideoUrl(target.substr(SEARCH_PREFIX.size()), cancelled);
        if (url.empty()) {
            return {};
        }
    }

    std::vector<std::string> arguments = YT_DLP_SONG_ARGS;
    arguments.push_back(url);
    IProcessRunner::Output run = runYtDlp(arguments, cancelled);
    const std::vector<std::string> &lines = run.lines;

    if (run.cancelled) {
        return {};
    }
    if (run.exitCode != 0 || lines.empty() || lines.back().rfind("http", 0) != 0) {
        qDebug() << "yt-dlp did not return a usable URL, exit code:" << run.exitCode;
        return {};
    }

    // Line order matches the --print flags: title, webpage_url, direct url.
    ResolvedMedia media;
    media.directUrl = lines.back();
    if (lines.size() >= 3) {
        media.title = lines[0];
        media.webpageUrl = lines[1];
    } else if (lines.size() == 2) {
        media.title = lines[0];
    }
    return media;
}

PlaylistListing YtDlpResolver::listPlaylist(const std::string &playlistUrl, size_t maxEntries)
{
    // --flat-playlist lists entries without extracting any video: a hundred
    // take 2 to 10 seconds. Nothing is printed before the last page asked
    // for is fetched. Each entry prints as a url/title line pair; the
    // "playlist:" prints come once, after them.
    const std::vector<std::string> arguments = {
        "--flat-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8",
        "--playlist-items", ":" + std::to_string(maxEntries),
        "--print", "url", "--print", "title",
        "--print", "playlist:PLAYLIST_TITLE=%(title)s",
        "--print", "playlist:PLAYLIST_COUNT=%(playlist_count)s",
        playlistUrl,
    };
    IProcessRunner::Output run = runYtDlp(arguments, {});
    if (run.exitCode != 0) {
        qDebug() << "yt-dlp playlist listing exit code:" << run.exitCode;
    }
    PlaylistListing listing;
    if (run.timedOut) {
        listing.timedOut = true;
        return listing;
    }

    const std::string titleMarker = "PLAYLIST_TITLE=";
    const std::string countMarker = "PLAYLIST_COUNT=";
    std::string pendingUrl;
    for (const std::string &line : run.lines) {
        if (line.rfind(titleMarker, 0) == 0) {
            std::string title = line.substr(titleMarker.size());
            listing.title = (title == "NA") ? std::string{} : title;
        } else if (line.rfind(countMarker, 0) == 0) {
            listing.totalCount = std::strtoul(line.c_str() + countMarker.size(), nullptr, 10); // "NA" -> 0
        } else if (pendingUrl.empty()) {
            pendingUrl = line;
        } else {
            // YouTube keeps placeholder entries for videos nobody can play.
            // A private one now shows up with no title at all ("NA").
            if (line != "[Private video]" && line != "[Deleted video]" && line != "NA") {
                listing.entries.push_back({pendingUrl, line});
            }
            pendingUrl.clear();
        }
    }
    if (!pendingUrl.empty()) {
        qDebug() << "Playlist listing ended mid-entry - dropped" << QString::fromStdString(pendingUrl);
    }
    return listing;
}
