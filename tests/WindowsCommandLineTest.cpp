#include "WindowsCommandLine.h"
#include "Check.h"

#include <string>
#include <vector>

using windows::commandLine;

int main()
{
    { // when an entry is quoted
        check(commandLine("yt-dlp", {"--no-warnings", "-f", "bestaudio"}) == "yt-dlp --no-warnings -f bestaudio",
              "plain entries stay bare");
        check(commandLine("yt-dlp", {"two words"}) == R"(yt-dlp "two words")", "an entry with a space is quoted");
        check(commandLine("yt-dlp", {"two\twords"}) == "yt-dlp \"two\twords\"", "an entry with a tab is quoted");
        check(commandLine("yt-dlp", {""}) == R"(yt-dlp "")", "an empty entry is \"\"");
        check(commandLine("yt-dlp", {R"(C:\Users\kamil\cookies.txt)"}) == R"(yt-dlp C:\Users\kamil\cookies.txt)",
              "a Windows path needs no quotes: its backslashes stay");
        check(commandLine(R"(C:\Program Files\yt-dlp\yt-dlp.exe)", {"--version"})
              == R"("C:\Program Files\yt-dlp\yt-dlp.exe" --version)",
              "a program path with a space is quoted, its backslashes stay");
    }

    { // backslashes and quotes
        check(commandLine("yt-dlp", {R"(a"b)"}) == R"(yt-dlp "a\"b")", R"(a"b gives "a\"b")");
        check(commandLine("yt-dlp", {R"(a\"b)"}) == R"(yt-dlp "a\\\"b")", R"(a\"b gives "a\\\"b")");
        check(commandLine("yt-dlp", {R"(a b\)"}) == R"(yt-dlp "a b\\")", R"(a b\ gives "a b\\")");
    }

    { // the command lines the bot sends
        check(commandLine("yt-dlp", {"--no-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8",
                                     "-f", "bestaudio", "--print", "title", "--print", "webpage_url", "--print", "urls",
                                     "https://www.youtube.com/watch?v=dQw4w9WgXcQ"})
              == "yt-dlp --no-playlist --no-warnings --socket-timeout 10 --encoding utf-8 -f bestaudio"
                 " --print title --print webpage_url --print urls https://www.youtube.com/watch?v=dQw4w9WgXcQ",
              "a song");
        check(commandLine("yt-dlp", {"--flat-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8",
                                     "--print", "%(ie_key)s %(url)s", "ytsearch5:never gonna"})
              == "yt-dlp --flat-playlist --no-warnings --socket-timeout 10 --encoding utf-8"
                 R"( --print "%(ie_key)s %(url)s" "ytsearch5:never gonna")",
              "a search with a two-word query");
        check(commandLine("yt-dlp", {"--flat-playlist", "--no-warnings", "--socket-timeout", "10", "--encoding", "utf-8",
                                     "--playlist-items", ":100", "--print", "url", "--print", "title",
                                     "--print", "playlist:PLAYLIST_TITLE=%(title)s",
                                     "--print", "playlist:PLAYLIST_COUNT=%(playlist_count)s",
                                     "https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLx"})
              == "yt-dlp --flat-playlist --no-warnings --socket-timeout 10 --encoding utf-8 --playlist-items :100"
                 " --print url --print title --print playlist:PLAYLIST_TITLE=%(title)s"
                 " --print playlist:PLAYLIST_COUNT=%(playlist_count)s"
                 " https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLx",
              "a playlist");
    }

    return summary();
}
