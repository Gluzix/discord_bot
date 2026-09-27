#include "YtDlpResolver.h"
#include "Check.h"

#include <QDebug>
#include <QString>

#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using Output = IProcessRunner::Output;
using Arguments = std::vector<std::string>;

// Records what it is asked and answers each run from the script, in order.
class ScriptedRunner : public IProcessRunner
{
public:
    struct Call
    {
        std::string program;
        Arguments arguments;
        std::chrono::seconds timeout{0};
        bool cancelled{false}; // what the caller's cancel check said
    };

    Output run(const std::string &program, const Arguments &arguments,
               std::chrono::seconds timeout, const CancelCheck &cancelled) override
    {
        calls.push_back({program, arguments, timeout, cancelled && cancelled()});
        if (script.empty()) {
            return {};
        }
        Output answer = script.front();
        script.pop_front();
        return answer;
    }

    Call call(size_t i) const
    {
        return i < calls.size() ? calls[i] : Call{};
    }

    std::deque<Output> script;
    std::vector<Call> calls;
};

static const std::string YT_DLP = R"(C:\tools\yt-dlp.exe)";
static const std::string SONG = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
static const std::string FOUND = "https://www.youtube.com/watch?v=first";
static const std::string DIRECT = "https://rr1---sn-x.googlevideo.com/videoplayback?expire=1";
static const std::string PLAYLIST = "https://www.youtube.com/playlist?list=PLx";

static std::vector<std::string> logged;

static void keepLog(QtMsgType, const QMessageLogContext &, const QString &message)
{
    logged.push_back(message.toStdString());
}

static bool wasLogged(const std::string &line)
{
    return std::find(logged.begin(), logged.end(), line) != logged.end();
}

static Output ended(int exitCode, std::vector<std::string> lines)
{
    Output output;
    output.started = true;
    output.exitCode = exitCode;
    output.lines = std::move(lines);
    return output;
}

static Arguments songArguments(const std::string &url)
{
    return {"--no-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8", "-f", "bestaudio",
            "--print", "title", "--print", "webpage_url", "--print", "urls", url};
}

static ResolvedMedia resolveSong(const Output &answer)
{
    auto runner = std::make_shared<ScriptedRunner>();
    runner->script = {answer};
    return YtDlpResolver(runner, YT_DLP).resolveMedia(SONG, {});
}

static PlaylistListing listPlaylist(std::vector<std::string> lines)
{
    auto runner = std::make_shared<ScriptedRunner>();
    runner->script = {ended(0, std::move(lines))};
    return YtDlpResolver(runner, YT_DLP).listPlaylist(PLAYLIST, 100);
}

static bool is(const PlaylistEntry &entry, const std::string &webpageUrl, const std::string &title)
{
    return entry.webpageUrl == webpageUrl && entry.title == title;
}

int main()
{
    qInstallMessageHandler(keepLog);

    { // what a song asks for
        auto runner = std::make_shared<ScriptedRunner>();
        runner->script = {ended(0, {"Song", SONG, DIRECT})};
        YtDlpResolver(runner, YT_DLP).resolveMedia(SONG, [] { return true; });
        const ScriptedRunner::Call call = runner->call(0);
        check(runner->calls.size() == 1, "a song: one run");
        check(call.program == YT_DLP, "a song: the program the resolver was given");
        check(call.timeout == std::chrono::seconds(60), "a song: 60 s");
        check(call.arguments == songArguments(SONG), "a song: its arguments, the url last");
        check(call.cancelled, "a song: the caller's cancel check reaches the run");
    }

    { // what a search asks for
        auto runner = std::make_shared<ScriptedRunner>();
        runner->script = {ended(0, {"Youtube " + FOUND}), ended(0, {"Song", FOUND, DIRECT})};
        YtDlpResolver(runner, YT_DLP).resolveMedia("ytsearch1:never gonna", {});
        check(runner->calls.size() == 2, "a search: two runs");
        check(runner->call(0).program == YT_DLP && runner->call(1).program == YT_DLP,
              "a search: the program the resolver was given, twice");
        check(runner->call(0).timeout == std::chrono::seconds(60)
                  && runner->call(1).timeout == std::chrono::seconds(60),
              "a search: 60 s, twice");
        check(runner->call(0).arguments == Arguments{"--flat-playlist", "--no-warnings", "--socket-timeout", "10",
                                                     "--encoding", "utf-8", "--print", "%(ie_key)s %(url)s",
                                                     "ytsearch5:never gonna"},
              "a search: a flat listing of five results first");
        check(runner->call(1).arguments == songArguments(FOUND), "a search: then the song's run on the video found");
    }

    { // what a playlist asks for
        auto runner = std::make_shared<ScriptedRunner>();
        runner->script = {ended(0, {})};
        YtDlpResolver(runner, YT_DLP).listPlaylist(PLAYLIST, 100);
        const ScriptedRunner::Call call = runner->call(0);
        check(runner->calls.size() == 1, "a playlist: one run");
        check(call.program == YT_DLP, "a playlist: the program the resolver was given");
        check(call.timeout == std::chrono::seconds(60), "a playlist: 60 s");
        check(call.arguments == Arguments{"--flat-playlist", "--no-warnings", "--socket-timeout", "10",
                                          "--encoding", "utf-8", "--playlist-items", ":100",
                                          "--print", "url", "--print", "title",
                                          "--print", "playlist:PLAYLIST_TITLE=%(title)s",
                                          "--print", "playlist:PLAYLIST_COUNT=%(playlist_count)s", PLAYLIST},
              "a playlist: its arguments, the url last");
    }

    { // a song's answer
        const ResolvedMedia three = resolveSong(ended(0, {"Song", SONG, DIRECT}));
        check(three.title == "Song" && three.webpageUrl == SONG && three.directUrl == DIRECT,
              "three lines: title, page and direct url");
        const ResolvedMedia two = resolveSong(ended(0, {"Song", DIRECT}));
        check(two.title == "Song" && two.webpageUrl.empty() && two.directUrl == DIRECT,
              "two lines: title and direct url, no page");
        const ResolvedMedia one = resolveSong(ended(0, {DIRECT}));
        check(one.title.empty() && one.webpageUrl.empty() && one.directUrl == DIRECT,
              "one line: the direct url alone");
    }

    { // a song that fails
        check(resolveSong(ended(0, {"Song", SONG, "NA"})).directUrl.empty(), "a last line that is no url: nothing");
        check(resolveSong(ended(1, {"Song", SONG, DIRECT})).directUrl.empty(), "a non-zero exit code: nothing");

        Output killed = ended(1, {"Song", SONG});
        killed.cancelled = true;
        logged.clear();
        check(resolveSong(killed).directUrl.empty() && logged.empty(), "a cancelled run: nothing, and quietly");

        logged.clear();
        check(resolveSong(Output{}).directUrl.empty(), "a run that never started: nothing");
        check(wasLogged("Could not start yt-dlp - put it on PATH or set YT_DLP_PATH"),
              "a run that never started: the hint is logged");
    }

    { // a search picks the first plain video
        auto runner = std::make_shared<ScriptedRunner>();
        runner->script = {ended(0, {"YoutubeTab https://www.youtube.com/@band",
                                    "YoutubeTab https://www.youtube.com/playlist?list=PLband",
                                    "Youtube " + FOUND,
                                    "Youtube https://www.youtube.com/watch?v=second"}),
                          ended(0, {"Song", FOUND, DIRECT})};
        const ResolvedMedia media = YtDlpResolver(runner, YT_DLP).resolveMedia("ytsearch1:band", {});
        check(runner->call(1).arguments.back() == FOUND, "a search: the first Youtube line, past the YoutubeTab ones");
        check(media.title == "Song" && media.webpageUrl == FOUND && media.directUrl == DIRECT,
              "a search: the video found, resolved");
    }

    { // a search with no video
        auto runner = std::make_shared<ScriptedRunner>();
        runner->script = {ended(0, {"YoutubeTab https://www.youtube.com/@band"})};
        const ResolvedMedia media = YtDlpResolver(runner, YT_DLP).resolveMedia("ytsearch1:band", {});
        check(media.directUrl.empty(), "no video found: nothing");
        check(runner->calls.size() == 1, "no video found: no second run");
    }

    { // a playlist
        const PlaylistListing listing = listPlaylist({
            "https://www.youtube.com/watch?v=a", "Song A",
            "https://www.youtube.com/watch?v=b", "[Private video]",
            "https://www.youtube.com/watch?v=c", "[Deleted video]",
            "https://www.youtube.com/watch?v=d", "NA",
            "https://www.youtube.com/watch?v=e", "Song E",
            "PLAYLIST_TITLE=Mix", "PLAYLIST_COUNT=250"});
        check(listing.title == "Mix", "a playlist: its title");
        check(listing.totalCount == 250, "a playlist: its count");
        check(listing.entries.size() == 2 && is(listing.entries[0], "https://www.youtube.com/watch?v=a", "Song A")
                  && is(listing.entries[1], "https://www.youtube.com/watch?v=e", "Song E"),
              "a playlist: private, deleted and NA titles are left out");
    }

    { // a playlist that names neither
        const PlaylistListing listing = listPlaylist({
            "https://www.youtube.com/watch?v=a", "Song A",
            "PLAYLIST_TITLE=NA", "PLAYLIST_COUNT=NA"});
        check(listing.title.empty(), "NA title: no title");
        check(listing.totalCount == 0, "NA count: unknown");
        check(listing.entries.size() == 1, "NA title and count: the entry stays");
    }

    { // a playlist that ends mid-entry
        const PlaylistListing listing = listPlaylist({
            "https://www.youtube.com/watch?v=a", "Song A",
            "https://www.youtube.com/watch?v=b"});
        check(listing.entries.size() == 1 && is(listing.entries[0], "https://www.youtube.com/watch?v=a", "Song A"),
              "ended mid-entry: the whole entries stay, the last url goes");
    }

    return summary();
}
