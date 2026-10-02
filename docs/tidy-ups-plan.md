# The small things, in one branch

Branch `tidy-ups` off origin/master (a67e241). The work happens in a
worktree of its own, with a build directory of its own, so that the
owner's tree stays free. This plan and `playlist-cap-findings.md` are
untracked files of the owner's tree and are not in the worktree; the brief
names them by their full path.

Seven commits, in the order below, each built, tested and committed before
the next is begun. The plan went through its checks on 2026-09-29; what it
states as a fact was measured or read in the sources.

## 1. cacert.pem is found next to the executable

Today `main.cpp` sets `SSL_CERT_FILE` to the bare name `cacert.pem`, so
OpenSSL looks for it in the directory the bot is started from. The build
copies the file next to the executable. Started from any other directory,
for example one that holds `token.json` and the logs, every connection
fails with "Malformed HTTP response".

The fix: `pointOpenSslAtBundledCertificates()` sets the full path of
`cacert.pem` in the executable's directory. `token.json` and `logs/` stay
in the directory the bot is started from.

- `main.cpp`: the executable's path comes from `GetModuleFileNameW`. The
  value is
  `std::filesystem::path(exePath).replace_filename("cacert.pem").u8string()`
  (a `std::string` in C++17) and goes to both `_putenv_s` calls.
  `#include <filesystem>` and `#include <string>` go after
  `#include <cstdlib>`.
- Why UTF-8: OpenSSL 1.1.1k, which DPP ships, opens the name as UTF-8
  first and in the ANSI code page only as a fallback. `.string()` would
  convert to the code page: a folder named `Łukasz` then works through the
  fallback only, and for a folder named `Жук` the conversion throws at
  startup.
- A `SSL_CERT_FILE` that is already set still wins, as today:
  `environmentVariableIsSet()` is unchanged.
- The comment above the function stays and ends with one more sentence:
  "OpenSSL opens the path as UTF-8."
- `README.md`:
  - the paragraph "The build copies `dpp.dll` and `cacert.pem` next to
    the executable" gets the sentence "The bot looks for `cacert.pem`
    there, wherever it is started from, unless `SSL_CERT_FILE` names
    another CA bundle.";
  - in Configuration and Logs, "the directory the bot runs from" and "the
    directory it runs from" become "the directory the bot is started
    from" and "the directory it is started from", so that one phrase names
    the working directory throughout.
- No test target: the function sets the environment of the process. The
  reviewers check it with a probe (Checks).

## 2. ChunkedSource gets a constructor and keeps its state to itself

Today `ChunkedSource` has an empty constructor and public data members,
and `PcmResampler::open()` fills in the url and the HTTP headers from
outside. No behaviour changes: the same requests, the same retries, the
same log lines.

- `Playback/ChunkedSource.h`: the includes, the forward declaration and
  the rules block stay. The class reads:
  ```cpp
  class ChunkedSource
  {
  public:
      explicit ChunkedSource(std::string url_);
      ~ChunkedSource();

      ChunkedSource(const ChunkedSource &) = delete;
      ChunkedSource &operator=(const ChunkedSource &) = delete;

      static int readCallback(void *opaque, uint8_t *buf, int size);
      static int64_t seekCallback(void *opaque, int64_t offset, int whence);

  private:
      bool fetchChunkAt(int64_t offset);
      int read(uint8_t *buf, int size);
      int64_t seek(int64_t offset, int whence);

      static constexpr int64_t FIRST_CHUNK_BYTES = 256 * 1024;  // small: audio starts fast even on a slow link
      static constexpr int64_t CHUNK_BYTES = 2 * 1024 * 1024;   // ~2 minutes of 128kbps audio

      std::string url;
      AVDictionary *httpOptions{nullptr};
      int64_t fileSize{-1};       // from the first response's Content-Range
      int64_t position{0};        // next byte the demuxer will read
      int64_t chunkStart{0};
      std::vector<uint8_t> chunk; // bytes [chunkStart, chunkStart + chunk.size())
  };
  ```
  No comment on the deleted copies, as in the neighbours.
- `Playback/ChunkedSource.cpp`, in place of the empty constructor:
  ```cpp
  ChunkedSource::ChunkedSource(std::string url_)
      : url(std::move(url_))
  {
      av_dict_set(&httpOptions, "headers",
                  "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                  0);
  }
  ```
  The header text moves as it is and stays inline, like the option names
  beside it. `#include <utility>` joins the standard includes: the file
  includes what it uses. `#include <libavformat/avformat.h>` goes: nothing
  of it is used. The other includes stay (`<chrono>` is the wait between
  the retries in `fetchChunkAt()`).
- `Playback/PcmResampler.cpp`, in `open()`:
  `source = std::make_unique<ChunkedSource>(directUrl);`, and the
  assignment of the url and the `av_dict_set` of the headers go.
  `#include <libavutil/dict.h>` goes with them: that call was its only
  use.
- No test target: without the network FFmpeg can feed the class from a
  local file only, and its file protocol ignores the offsets, so the
  ranges and the retries cannot be tested that way.

## 3. Log takes a directory whose name is outside the code page

Found by the check of item 1. Started from a directory whose name has a
letter outside the ANSI code page (`Жук`), the bot ends at once:
`openLogFile()` turns the log file's path into text with `.string()`,
which throws, and nothing catches it. With a name inside the code page
(`Łukasz`) it works, but the path in the header line is written in the
code page while the rest of the file is UTF-8.

- `Helpers/Log.cpp`: in `openLogFile()` the path of the header line is
  taken with `.u8string()`; in `logFilesOldestFirst()` the file name is
  taken with `.u8string()` too (a file in `logs/` with such a name throws
  in the same way).
- Nothing else in `Log` changes.
- The test of item 4 works in a directory with such a name, so it stands
  in for this fix.

## 4. A test for the log files

`Helpers/Log` has no test. A template that passes three runs in a row
against `Log` with the fix of item 3 lies in the session's scratch folder;
the brief names it. The coder takes it as a start and makes it read like
the tests beside it.

- `tests/LogTest.cpp`, added to `tests/CMakeLists.txt` in the three
  groups of lines, like `InteractionsTest`:
  ```cmake
  add_bot_test(LogTest LogTest.cpp Check.h ../Helpers/Log.cpp)
  target_link_libraries(LogTest PRIVATE dpp Qt${QT_VERSION_MAJOR}::Core)
  set_tests_properties(LogTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH};${QT_PATH}")
  ```
- The test works in
  `std::filesystem::temp_directory_path() / std::filesystem::u8path(u8"discord_bot_LogTest_Жук")`.
  Without `u8path` the name would be read in the code page and come out
  as another one. It removes that directory if it is there, makes it with
  `logs/` inside, and makes it the current directory until its last
  check: `Log` opens `logs/` relative to the current directory at every
  rotation. It never touches a `logs/` that was there before.
- At the end it steps out and calls
  `std::filesystem::remove_all(directory, error)` and ignores the error.
  Windows refuses the one file `Log` still holds open (its header line
  only, about 150 bytes), so that file and its two directories stay until
  the next run removes them.
- The test never throws, aborts or hangs. In a debug build an uncaught
  exception becomes a dialog on the owner's screen, and `ctest` has no
  time limit here.
  - `main()` begins with the calls that send the debug runtime's reports
    to stderr and keep its dialogs away (`_set_abort_behavior`,
    `_CrtSetReportMode` and `_CrtSetReportFile` for warnings, errors and
    assertions).
  - Only the `error_code` overloads of `std::filesystem`.
  - Every path that holds the test's directory becomes text with
    `.u8string()`, never `.string()`, which throws for that name.
  - `try { logging::install(); } catch (const std::exception &) { check(false, "install: no exception"); return summary(); }`:
    the catch ends the test, because the rotation would throw again.
  - The loop that fills the file writes at most 11 MiB, so a `Log` that
    never rotates fails instead of hanging.
- Sizes are taken with `std::filesystem::file_size(path, error)`, never
  from a directory entry: Windows brings the size in a listing up to date
  only when the file is closed, and `Log` keeps its file open.
- One process, `install()` once, in this order:
  1. Before `install()`, `logs/` holds
     `discord_bot-20000101-000000.log` (10 MiB),
     `discord_bot-20000102-000000.log` (20 MiB),
     `discord_bot-20000103-000000.log` (25 MiB) and `bot.log` (1 MiB),
     each created with `std::ofstream` and brought to its size with
     `std::filesystem::resize_file(path, size, error)`.
     `nameThisThread("main")` comes first.
  2. After `install()`, `logs/` holds `bot.log`, the files of the 2nd and
     the 3rd, and one new file named
     `discord_bot-<8 digits>-<6 digits>.log`. The file of the 1st is gone:
     55 MiB are past 50, so the oldest goes. `bot.log` stays: only names
     that start with `discord_bot-` and end in `.log` are log files, and a
     `Log` without that filter would delete it first. The new file's
     first line is `<time> [main] INFO: Logging to ` and then
     `std::filesystem::absolute(std::filesystem::path("logs") / name, error).u8string()`,
     the call `openLogFile()` makes after item 3.
  3. `qDebug("a line from Qt")` makes the file's last line
     `<time> [main] DEBUG: a line from Qt`, `<time>` being
     `YYYY-MM-DD HH:MM:SS.mmm`.
  4. A `dpp::log_t` built by hand, with `ll_trace`, handed to
     `logging::dppLog()`, makes the file's last line
     `<time> [main] TRACE: <message>`, and nothing of it reaches the
     console: the test swaps the buffers of `std::cout` and `std::cerr`
     around that one call and finds them empty.
  5. The test waits until the clock has left the second in which
     `install()` returned (see below), then writes TRACE lines of 1 KiB
     until the file reaches 10 MiB. A second file exists then, the first
     ends with the line that crossed the limit (under 10 MiB and 2 KiB),
     the second holds its header line only, and the file of the 2nd is
     gone (20 + 25 + 10 MiB are past 50).
- The open file is not checked for being kept: Windows refuses to delete
  a file that is open, so that check could not fail.
- One run takes about a second and prints about fifteen lines.
- `README.md`: the tests sentence names the log files.

Known and left as it is: `Log` names its files to the second. A file that
fills within the second it was opened is opened again under its own name
with every further line (measured: up to 1410 times for 11 MiB). The bot
cannot get there (10 MiB of log in one second), so `Log` stays as it is,
and the test waits for the next second before it fills the file: one wait
loop with a one-line comment. Also left as it is: a second `install()` in
one process stops the file without a word; the bot calls it once.

## 5. The comment about listing a playlist says what was measured

`Resolver/YtDlpResolver.cpp`, in `listPlaylist()`: "even a 6000-video
playlist answers in a few seconds" is not true (5000 entries took 27 to
37 seconds), and the comment leaves out what decides the meaning of a
timeout. It becomes:
```cpp
    // --flat-playlist lists entries without extracting any video: a hundred
    // take 2 to 10 seconds. Nothing is printed before the last page asked
    // for is fetched. Each entry prints as a url/title line pair; the
    // "playlist:" prints come once, after them.
```
Nothing else changes.

## 6. A mix is queued without its repeats

A list whose id starts with `RD` is made by YouTube, never by a person.
Most are mixes; YouTube Music's own playlists start with `RD` too. A
person's playlist starts with `PL`. `/playlist` takes them all like a
playlist.

Three mix listings were kept, from two seeds. The two long ones repeat
from position 26 on: 18 and 20 of their first 100 entries are songs the
list already had. The one listed with the bot's own arguments had no
repeat. YouTube sends a different mix each time. A repeat has the same
url, byte for byte.

The fix: for a mix, an entry whose url is among the entries already kept
is left out. Placeholders ("[Private video]", "[Deleted video]", "NA")
are left out first and count as nothing kept, so a video whose first
appearance came back as "NA" is kept where it comes again. A playlist
that a person made keeps its repeats.

- `Helpers/YoutubeInput.h`, after `isPlaylistPageUrl()`:
  ```cpp
  // A list YouTube made up (list=RD...), not one a person put together.
  bool isMixUrl(const std::string &url);
  ```
  `Helpers/YoutubeInput.cpp`, at the end:
  ```cpp
  bool youtube::isMixUrl(const std::string &url)
  {
      return url.find("?list=RD") != std::string::npos || url.find("&list=RD") != std::string::npos;
  }
  ```
  `list` must be a parameter of its own. A name that ends in `list`
  (`playlist=RD...`) and an `RD` at the start of another value
  (`v=RD...`) do not make a mix.
- `Resolver/YtDlpResolver.cpp`: `#include "YoutubeInput.h"` under
  `#include "YtDlpResolver.h"`, `#include <unordered_set>` between
  `<cstdlib>` and `<utility>`. In `listPlaylist()`, after `countMarker`:
  ```cpp
      const bool mix = youtube::isMixUrl(playlistUrl);
      std::unordered_set<std::string> keptUrls;
  ```
  and the entry's condition with its comment becomes:
  ```cpp
              // YouTube keeps placeholder entries for videos nobody can play.
              // A private one now shows up with no title at all ("NA"). A mix
              // comes back round to songs it already had.
              if (line != "[Private video]" && line != "[Deleted video]" && line != "NA"
                  && (!mix || keptUrls.insert(pendingUrl).second)) {
  ```
  The why stands at the condition, next to the placeholders', and not in
  the rules block.
- `Resolver/IMediaResolver.h`, the comment of `listPlaylist()`:
  ```cpp
      // Lists the first maxEntries videos of a YouTube playlist without
      // resolving any of them. Private and deleted videos are left out, and
      // so are the repeats of a mix.
      // A listing that timed out comes back empty, with timedOut set.
  ```
- `tests/CMakeLists.txt`: the `YtDlpResolverTest` line gains
  `../Helpers/YoutubeInput.cpp`. No include path changes.
- `tests/YtDlpResolverTest.cpp`: the helper becomes
  `static PlaylistListing listPlaylist(std::vector<std::string> lines, const std::string &link = PLAYLIST)`
  and hands `link` on; its callers stay. Two blocks after "a playlist that
  ended with an error":
  ```cpp
      { // a mix
          const std::vector<std::string> lines = {
              "https://www.youtube.com/watch?v=a", "Song A",
              "https://www.youtube.com/watch?v=b", "NA",
              "https://www.youtube.com/watch?v=c", "Song C",
              "https://www.youtube.com/watch?v=a", "Song A",
              "https://www.youtube.com/watch?v=b", "Song B",
              "PLAYLIST_TITLE=Mix - Song A", "PLAYLIST_COUNT=NA"};
          const PlaylistListing mix = listPlaylist(lines, "https://www.youtube.com/watch?v=a&list=RDa&start_radio=1");
          check(mix.entries.size() == 3 && is(mix.entries[0], "https://www.youtube.com/watch?v=a", "Song A")
                    && is(mix.entries[1], "https://www.youtube.com/watch?v=c", "Song C")
                    && is(mix.entries[2], "https://www.youtube.com/watch?v=b", "Song B"),
                "a mix: repeats left out, the first playable one kept, in order");
          check(listPlaylist(lines).entries.size() == 4, "a playlist: its repeats stay");
      }

      { // which links are mixes
          const std::vector<std::string> twice = {
              "https://www.youtube.com/watch?v=a", "Song A",
              "https://www.youtube.com/watch?v=a", "Song A"};
          check(listPlaylist(twice, "https://www.youtube.com/playlist?list=RDCLAK5uy_a").entries.size() == 1,
                "list=RD first in the query: a mix");
          check(listPlaylist(twice, "https://youtu.be/a?si=b&list=RDa&index=3").entries.size() == 1,
                "list=RD further on in the query: a mix");
          check(listPlaylist(twice, "https://www.youtube.com/watch?v=RDa&list=PLa").entries.size() == 2,
                "RD in another parameter: no mix");
          check(listPlaylist(twice, "https://www.youtube.com/watch?v=a&playlist=RDa&list=PLa").entries.size() == 2,
                "a parameter whose name ends in list: no mix");
      }
  ```
- `README.md`: nothing.

## 7. A reply that can no longer be edited is left alone

A `/play` that waits answers "Queued at position N". The resolver works
on the first five waiting songs only, so a song queued at position 6 or
later gets its edit to "Queued at position N: title" when it moves up
among them. When that takes more than 15 minutes, Discord refuses the
edit: the edit goes through the interaction's token, which Discord keeps
for 15 minutes. Nobody sees the refusal: the edit has no callback, and
dpp 10.1.6 logs no refused request.

The fix: the edit is not sent once 14 minutes have passed since the
interaction was made, and one line in the log says so.

- The time comes from the interaction's id:
  `event.command.id.get_creation_time()`, seconds since 1970 as a
  `double`, the call `interactions::ageMs()` makes already. An id of 0
  reads as 2015 and counts as too old.
- Why 14: Discord's documentation gives the token 15 minutes without
  naming when they start; the time in the id is the earliest they could
  start. The minute covers a request that waits in dpp's queue and a
  clock that is off.
- `Helpers/Interactions.h`, after `ageMs()`:
  ```cpp
  // Whether the reply can still be edited at nowSeconds, a unix time.
  bool tokenAlive(const dpp::interaction_create_t &event, int64_t nowSeconds);
  ```
  and one more bullet in its rules block:
  ```cpp
  // - tokenAlive() gives a token 14 minutes from the time in the interaction's
  //   id: Discord keeps it 15, and the minute covers a request still queued in
  //   dpp and a clock that is off.
  ```
- `Helpers/Interactions.cpp`: in the unnamed namespace, above `isClick()`,
  `constexpr int64_t TOKEN_ALIVE_SECONDS = 14 * 60;`, and after `ageMs()`:
  ```cpp
  bool tokenAlive(const dpp::interaction_create_t &event, int64_t nowSeconds)
  {
      return nowSeconds - event.command.id.get_creation_time() < TOKEN_ALIVE_SECONDS;
  }
  ```
- `Playback/ResolverWorker.cpp` includes `"Interactions.h"`. In
  `resolveSongs()` the check goes under `stateMutex`, before the copy of
  the event, so that a reply that is too old costs neither the copy nor a
  request. It sends nothing, so the rule "no dpp call under it" stays
  true. The block becomes:
  ```cpp
                  if (!song.fromPlaylist && song.failedAttempts == 0) {
                      if (interactions::tokenAlive(*song.event, song.resolvedAtSeconds)) {
                          label = labels::render(song.title, song.webpageUrl, song.target);
                          position = i + 1;
                          requestEvent = std::make_unique<dpp::slashcommand_t>(*song.event);
                      } else {
                          qDebug() << "The \"Queued at position\" reply is too old to edit - it stays without the title";
                      }
                  }
  ```
- `tests/InteractionsTest.cpp`, the helper after `slash()`:
  ```cpp
  // A snowflake's top bits count milliseconds from 2015-01-01, Discord's epoch.
  static dpp::snowflake idMadeAt(uint64_t unixSeconds)
  {
      return dpp::snowflake((unixSeconds * 1000 - 1420070400000ull) << 22);
  }
  ```
  and after the last block:
  ```cpp
      { // the token of a reply
          const int64_t madeAt = 1790000000;
          dpp::slashcommand_t event = slash();
          event.command.id = idMadeAt(madeAt);
          check(event.command.id.get_creation_time() == madeAt, "token: the id carries the time the interaction was made");
          check(interactions::tokenAlive(event, madeAt + 14 * 60 - 1), "token: 13:59 after the interaction it can be edited");
          check(!interactions::tokenAlive(event, madeAt + 14 * 60), "token: 14:00 after, it is too old");
          event.command.id = 0;
          check(!interactions::tokenAlive(event, madeAt), "token: an interaction without an id is too old");
      }
  ```
  No CMake change.
- The other edits through the token come within a minute of the command,
  or are the "Playing:" of a song that started on an idle bot, whose
  failure `PlayingPanel::announced()` reports. They stay as they are.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class header, each bullet naming the function that enforces it. Code
that reads like its surroundings. C++17. Commit messages = short header +
`*` bullets, one line each, ending with the coding model's co-author
trailer. Nothing pushed. Nothing added or changed under `docs/`.
`token.json` is never read, printed, copied or touched. The bot is never
started, stopped or killed. DPP untouched. Zero warnings. History is
linear: no merge commits. Files are staged by name, never with
`git add -A` or `git commit -a`.

No program that an agent builds may open a dialog on the owner's screen:
every probe switches the error dialogs of the debug runtime off before
anything else and runs with a time limit. The brief carries the calls.

After the review and its fix commits the orchestrator removes the
worktree, so that the branch is checked out nowhere and the owner can
take it into his tree.

## Checks

- Build everything (bot and tests) in the build directory of the
  worktree; zero warnings in the ninja output; `ctest`: all tests pass,
  the new one included, twice in a row in the same build directory.
- Item 1, a probe in the reviewer's scratch folder, built `/MDd` like the
  bot, without the network. It loads DPP's `libssl-1_1-x64.dll` by its
  full path, runs the two functions pasted from the worktree's
  `main.cpp`, makes DPP's own call `SSL_CTX_set_default_verify_paths`,
  and counts the certificates in the store. `cacert.pem` lies beside the
  probe. Started from another directory it loads 150 certificates with
  the probe in a folder with an ASCII name, in one named `Łukasz`, and in
  one named `Жук`, which only UTF-8 passes. With `SSL_CERT_FILE` set
  beforehand to another copy, that value stays and 150 load; set to a
  missing file, it stays and 0 load. A tested template of the probe lies
  in the session's scratch folder; the brief names it.
- Item 2: the bot builds, and `git diff` shows no change in what
  `fetchChunkAt()`, `read()` and `seek()` do.
- Items 6 and 7: copies of the changed sources, broken on purpose, fail
  the new test blocks.
- Owner's runtime pass:
  1. Play a song and seek in it: as before (item 2).
  2. In Qt Creator, set Projects > Run > Working directory to another
     directory that holds `token.json` and start the bot: it connects, and
     its logs go there (item 1). Set it back afterwards.
  3. `ctest` twice: the log test passes both times. Afterwards the
     temporary directory holds `discord_bot_LogTest_Жук\logs` with one log
     file of about 150 bytes, its header line, and nothing else is left
     (item 4).
  4. `/playlist` with a mix link (a song's link with `&list=RD...`, as
     YouTube's Mix gives it): the summary counts fewer than 100 songs.
     YouTube sends a different mix each time, and one of three listings
     had no repeat, so a summary of 100 is not a failure: try the link
     again. `/queue` right after it cannot show the fix: the repeats begin
     at position 26 (item 6).
  5. `/play` a video longer than 15 minutes, five more songs, then the
     song under test: it answers "Queued at position 6". After 14
     minutes, `/remove 1`: the song moves among the first five, its reply
     stays as it is, and the log has the line about a reply that is too
     old. A `/play` at position 5 or lower gets its title within seconds
     (item 7).
