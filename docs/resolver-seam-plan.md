# Two seams: who resolves a song, and who runs a process

Branch `resolver-seam`, on top of `panel-polish` (PR 33 touches the same
files). No behaviour changes for the user. The work prepares two things that
are coming, and builds neither:

- a Linux version, which needs its own way to run a process;
- the ytres library (`C:\workspace\yt-dlp-resolver`), which will resolve
  songs in-process, with yt-dlp kept as the second resolver behind the same
  interface.

YAGNI is the rule: no build flag, no factory, no Linux code, no second
resolver, no richer error reporting. Those arrive with their first user.
What is built is the cut itself, in the two places where the next
implementation will plug in.

## Today

`Runner/WindowsProcessRunner` does three jobs: it runs a process (Win32), it
knows yt-dlp (command lines, parsing), and it repairs titles that yt-dlp
wrote in the ANSI code page (Win32). `DecoderWorker`, `ResolverWorker` and
`PlaylistCommand` call its static functions.

## After

```
IMediaResolver                  what playback and /playlist ask for
 └─ YtDlpResolver               yt-dlp's command lines and answers, no OS code
      └─ IProcessRunner         run a program, get its lines back
           └─ WindowsProcessRunner   Win32: process, job object, pipe, code page
```

Interfaces carry the `I` prefix, as `ICommand` does.

## Files

1. `Runner/IProcessRunner.h`:
   ```cpp
   // Runs a program to its end and hands back what it wrote.
   // =======================================================
   // Rules:
   // - run() is bounded: it returns once the timeout has passed, and within
   //   a poll interval once cancelled() says so; what it started is dead by
   //   then, with everything that program spawned.
   // - The lines run() returns are UTF-8, whatever the program wrote.
   // - run() is called from several threads at once.
   // =======================================================
   class IProcessRunner
   {
   public:
       // Returns true once the caller no longer wants the result. Empty
       // means never.
       using CancelCheck = std::function<bool()>;

       struct Output
       {
           bool started{false};            // false: the program could not be run at all
           // 0 only when the program ended by itself with 0: never after
           // run() killed it, a crash or a failed start.
           int exitCode{1};
           bool cancelled{false};          // killed because the caller lost interest
           std::vector<std::string> lines; // stdout split into non-empty lines
       };

       virtual ~IProcessRunner() = default;

       virtual Output run(const std::string &program, const std::vector<std::string> &arguments,
                          std::chrono::seconds timeout, const CancelCheck &cancelled) = 0;
   };
   ```
   Arguments are a list, one entry per argument, unquoted: Linux hands such
   a list to `execvp`, Windows builds one command line from it.

2. `Runner/WindowsCommandLine.h/.cpp`: pure, no `windows.h`.
   ```cpp
   namespace windows {
   // One command line from a program and its arguments, quoted so that the
   // program started reads back exactly these arguments.
   std::string commandLine(const std::string &program, const std::vector<std::string> &arguments);
   }
   ```
   The rules are those of `CommandLineToArgvW` and the C runtime. An entry
   is quoted when it is empty or holds a space, a tab or a quote. Inside the
   quotes, n backslashes followed by a quote become 2n+1 backslashes and the
   quote, n backslashes at the very end become 2n, and every other backslash
   stays as it is. Entries are joined by one space.

3. `Runner/WindowsProcessRunner.h/.cpp`: implements `IProcessRunner`; what
   stays is today's `runYtDlp()` as `run()`, and the code page helpers.
   - The process, the job object, the suspended start, the pipe polling, the
     poll interval, the kill: unchanged. The timeout comes as a parameter.
     A run that was killed still reports exit code 1, as today.
   - The command line is `windows::commandLine(program, arguments)`, turned
     to UTF-16 as today.
   - Every output line goes through `ensureUtf8()` (today only the titles
     do; the other lines are ASCII, for which it changes nothing).
   - Log lines name the program instead of yt-dlp, printed with
     `noquote()` so a path keeps its single backslashes: `Could not start
     <program>, error: <n>`, `<program> killed after <n> s: <the last 60
     characters of the command line>` (cut from the QString, as today, so
     no character is split), `<program> runs outside a job object - a kill
     won't reach its children`, `Failed to create the pipes for <program>`.
   - The class title becomes the runner's own, e.g. "Runs a program with
     Win32 and hands back its output."
   - The rules block keeps the bullets about the job object (with its
     reason), the polling and `CreateProcessW`, naming `run()`; the polling
     bullet says "the timeout" where it named `YT_DLP_TIMEOUT_SECONDS`. The
     code page bullet keeps the fact and its reason: piped output can come
     in the ANSI code page (cp1250 here), and yt-dlp's sometimes does even
     with `--encoding utf-8`, which Discord shows as mojibake, so `run()`
     repairs every line. The bullet about yt-dlp's search moves to
     `YtDlpResolver.h`.
   - `YT_DLP_PATH`, the yt-dlp arguments and all parsing leave this class.

4. `Resolver/IMediaResolver.h` (new directory, added to the include paths):
   `ResolvedMedia`, `PlaylistEntry` and `PlaylistListing` move here
   unchanged, and
   ```cpp
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
       // resolving any of them. Private and deleted videos are left out.
       virtual PlaylistListing listPlaylist(const std::string &playlistUrl, size_t maxEntries) = 0;
   };
   ```
   No default argument on the virtual function; both callers pass their
   check already.

5. `Resolver/YtDlpResolver.h/.cpp`: implements `IMediaResolver` with yt-dlp.
   ```cpp
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
       std::string firstVideoUrl(const std::string &query, const CancelCheck &cancelled);
       ...
   };
   ```
   - `resolveMedia()`, `firstVideoUrl()` and `listPlaylist()` move over with
     their parsing word for word, minus the four `ensureUtf8()` calls
     (`run()` repairs every line now, and the resolver holds no OS code).
     Only the arguments change shape: each former string becomes a list with
     the same arguments in the same order, without the quotes that were part
     of the old command line (`--print`, `%(ie_key)s %(url)s` is two
     entries; the url or `ytsearch5:<query>` is the last one).
   - `runYtDlp()` calls the runner with `program`, the arguments and the 60 s
     timeout, and logs `Could not start yt-dlp - put it on PATH or set
     YT_DLP_PATH` when the output says it never started.
   - `programFromEnvironment()` reads `qEnvironmentVariable("YT_DLP_PATH")`
     (the one-argument form; `<QString>` is needed beside it) and falls back
     to `"yt-dlp"` when that is empty - a variable that is set but empty
     means PATH, as today. It is read once, at startup, instead of at every
     run.
   - The rules block gets the yt-dlp facts from `WindowsProcessRunner.h`:
     every call passes `--encoding utf-8`, without which yt-dlp writes the
     ANSI code page; a search picks the first plain video from a flat
     listing, with the reason the old bullet gives. Both name the functions
     as they are called here.

6. The resolver reaches its users through constructors:
   - `CommandHandler` gets a member `std::shared_ptr<IMediaResolver>
     resolver` (forward declaration in the header). `prepare()` builds
     `std::make_shared<YtDlpResolver>(std::make_shared<WindowsProcessRunner>(), YtDlpResolver::programFromEnvironment())`
     before the controller, and `setupCommands()` hands it to
     `PlaylistCommand`. This is the one line of resolver wiring that a Linux
     build or a second resolver changes.
   - `explicit PlaybackController(std::shared_ptr<IMediaResolver> resolver_)`
     keeps it in a member declared before `songPlayer` and `resolverWorker`
     (so it outlives both) and gives each a reference.
   - `SongPlayer(IMediaResolver &resolver_, std::function<void(std::string)> onLabelResolved_)`
     passes it on to `DecoderWorker(PcmBuffer &, Song &, IMediaResolver &, ...)`.
   - `ResolverWorker(IMediaResolver &resolver_, std::deque<Song> &, ...)`.
   - `PlaylistCommand(std::shared_ptr<PlaybackController>, std::shared_ptr<IMediaResolver>)`.
   - Includes: `PlaybackController.cpp`, `DecoderWorker.cpp`,
     `ResolverWorker.cpp` and `PlaylistCommand.cpp` include
     `IMediaResolver.h` in place of `WindowsProcessRunner.h`; only
     `CommandHandler.cpp` includes `YtDlpResolver.h` and
     `WindowsProcessRunner.h`. `PlaybackController.h`'s forward declaration
     of `PlaylistEntry` points at `IMediaResolver.h`.
   - The callers no longer know what resolves, so four comments say "the
     resolve" or "a resolve" where they say yt-dlp, the facts unchanged:
     `DecoderWorker.h` (the stop bullet), `ResolverWorker.h` (the
     `resolveSongs()` bullet), `SongPlayer.h` (the decoder that outlives
     `stop()`), `ResolverWorker.cpp` (the lookahead comment).

7. `Helpers/YoutubeInput.h` and `Helpers/Labels.h` stay as they are: what
   they say about yt-dlp is about yt-dlp.

8. Tests, in `tests/CMakeLists.txt`:
   ```cmake
   # inside add_bot_test: "${CMAKE_SOURCE_DIR}/Runner" with commit (1),
   # "${CMAKE_SOURCE_DIR}/Resolver" with commit (2)
   add_bot_test(WindowsCommandLineTest WindowsCommandLineTest.cpp Check.h ../Runner/WindowsCommandLine.cpp)
   add_bot_test(YtDlpResolverTest YtDlpResolverTest.cpp Check.h ../Resolver/YtDlpResolver.cpp)
   target_link_libraries(YtDlpResolverTest PRIVATE Qt${QT_VERSION_MAJOR}::Core)
   # after set(QT_PATH ...): Qt's DLL is found outside Qt Creator too
   set_tests_properties(YtDlpResolverTest PROPERTIES ENVIRONMENT_MODIFICATION "${QT_PATH}")
   ```
   - `tests/WindowsCommandLineTest.cpp` (no dpp, no Qt): plain entries stay
     bare; an entry with a space is quoted; an empty entry is `""`; `a"b`
     gives `"a\"b"`; `a\"b` gives `"a\\\"b"`; `a b\` gives `"a b\\"`;
     backslashes in an entry that needs no quotes stay as they are (a
     Windows path); a program path with a space; and the three command lines
     the bot sends today (song, search with a two-word query, playlist)
     spelled out in full.
   - `tests/YtDlpResolverTest.cpp` (a fake `IProcessRunner` that records
     what it was asked and answers from a script): the program, the timeout
     and the exact argument lists of the three calls; a song with three
     lines, with two, with one; a last line that is no url; a non-zero exit
     code; a cancelled run; a run that never started; a search that picks
     the first `Youtube` line and skips `YoutubeTab` ones, a search with no
     video, and that a search does two runs; a playlist with title and
     count, with `NA` for both, with `[Private video]`, `[Deleted video]`
     and `NA` titles left out, and one that ends mid-entry.

9. `CMakeLists.txt` (the new files next to `Runner/WindowsProcessRunner`,
   the `Resolver` include directory) and `README.md`: "How it works" says
   that `YtDlpResolver` builds yt-dlp's command lines and reads its answers
   behind `IMediaResolver`, and that `WindowsProcessRunner` runs the process
   behind `IProcessRunner`; the test sentence lists the two new tests.

## What changes for the running bot

Nothing a user sees. Under the hood:

- The command line is quoted by rule instead of by hand. A url or a
  one-word query no longer wears quotes it does not need; yt-dlp reads the
  same arguments.
- Every line of yt-dlp's output is checked for the code page, not only the
  titles.
- `YT_DLP_PATH` is read once, at startup.
- A yt-dlp that cannot be started gives three log lines instead of two: the
  error number from the runner, the hint from the resolver, and the parse
  line that follows both today.
- A Windows crash status of yt-dlp (an exit code above 2^31) logs as a
  negative number.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class or namespace header, each bullet naming the function that enforces
it. Code that reads like its surroundings. Commit messages = short header +
`*` bullets, one line each, ending with the coding model's co-author trailer.
Nothing pushed. Nothing added or changed under `docs/`. `token.json` is never
read, printed or touched. The bot is never started. DPP untouched. Zero
warnings. History is linear: no merge commits.

Commits, each building and passing on its own: (1)
`windows::commandLine()` with its test; (2) the two interfaces,
`YtDlpResolver`, the slimmer `WindowsProcessRunner`, the constructors, the
resolver test, the README.

## Checks

- Build everything after each commit (bot and tests), zero warnings.
- `ctest`: all tests pass, the two new ones included.
- Owner's runtime pass: `/play` with a link; `/play` with a two-word title
  and one with Polish letters (the title comes back readable); `/playlist`;
  a skip while "Looking for your song..." shows (yt-dlp is killed at once);
  a queued song that starts without a pause (the resolver thread did its
  work). Then once with `YT_DLP_PATH` pointing at a file that does not
  exist: `/play` answers "Couldn't get the audio from that link :(" and the
  log shows `Could not start <that path>, error: 2` followed by the hint.
  That one run proves that the variable is honoured and shows the new log
  lines.
