# File logging plan

Goal: every line the bot prints survives a closed terminal, carries a
timestamp and the name of the thread that wrote it, and DPP's own log lines
land in the same file. No new dependency.

## Today

- The bot's own messages go through `qDebug()` (19 call sites) to stderr,
  with no timestamp.
- DPP's messages go through `bot->on_log(dpp::utility::cout_logger())` in
  `CtrlMainServer.cpp`, to stdout, formatted `[Mon Sep 14 23:49:25 2026] DEBUG: ...`.
- `token.json` is read from the working directory; that is where the logs go.

## Design

New files `Helpers/Log.h` and `Helpers/Log.cpp` (Helpers is already an
include directory), namespace `logging`:

```cpp
namespace dpp { struct log_t; }

namespace logging {

// Routes every qDebug/qWarning/qCritical and every dpp log line into
// logs/discord_bot-<start time>.log (working directory) and still to the
// console. Call once at the top of main(), before anything logs.
void install();

// Names the calling thread for the log and for the debugger.
void nameThisThread(const char *name);

// The dpp on_log handler: bot->on_log(logging::dppLog);
void dppLog(const dpp::log_t &event);

}
```

Only the `.cpp` includes `<dpp/dpp.h>`; the header forward-declares `log_t`
so nothing else pulls dpp in.

### Line format

Same format for both sources:

```
2026-09-16 10:31:05.123 [sender] DEBUG: message
2026-09-16 10:31:05.130 [18344] WARN: Voice session error: ...
```

- Local time with milliseconds.
- Thread: the name given by `nameThisThread`, else the numeric thread id
  (`GetCurrentThreadId()`). Anything numeric is therefore one of DPP's
  threads.
- Level: for Qt messages map `QtDebugMsg/QtInfoMsg/QtWarningMsg/QtCriticalMsg/QtFatalMsg`
  to `DEBUG/INFO/WARN/ERROR/FATAL`; for dpp use
  `dpp::utility::loglevel(event.severity)` (already uppercase strings).
- The formatted line goes to the file and to the console (stderr for Qt
  messages as today, stdout for dpp lines as today), so Qt Creator's output
  pane shows the same text, now with timestamps.

### Sink

- `install()` creates `logs/` if missing, opens
  `logs/discord_bot-YYYYMMDD-HHMMSS.log` (start time), installs the Qt
  handler with `qInstallMessageHandler`, and writes one first line to both
  console and file: `Logging to <absolute path>`.
- If the directory or file cannot be created, say so once on the console
  and keep console logging only. Never abort the bot over a log file.
- One `std::mutex` around every write. The handler is called from every
  thread there is. The handler must never call `qDebug` itself (recursion).
- Flush after every line (`std::flush`). Volume is small; a crash or a
  closed terminal then loses nothing already written.
- Retention: after opening the new file, list `logs/discord_bot-*.log`,
  sort by name (the timestamp makes names sortable), delete all but the
  newest 20. Use `std::filesystem` (C++17 is on).
- Keep every level, including DPP debug: debug is what found the reconnect
  loop of issue #10.

### Thread names

`nameThisThread` stores the name in a `thread_local` and also calls
`SetThreadDescription(GetCurrentThread(), L"<name>")` (Windows 10 1607+,
`<windows.h>`/`<processthreadsapi.h>`; `WIN32_LEAN_AND_MEAN` before
windows.h as the other files do, to keep winsock.h out) so the name shows in
Process Explorer, the debugger and WER crash dumps.

Call sites, one line each, first statement of the function:

| Thread | Where | Name |
|---|---|---|
| main | `main()` in `main.cpp`, right after `logging::install()` | `main` |
| playback worker | `PlaybackController::playbackWorker()` | `worker` |
| resolver | `ResolverWorker::run()` | `resolver` |
| decoder | `SongPlayer::decode()` | `decoder` |
| sender | `SongPlayer::streamAudio()` | `sender` |

### Wiring

- `main.cpp`: `logging::install(); logging::nameThisThread("main");` as the
  first statements of `main()`, before `pointOpenSslAtBundledCertificates()`.
- `CtrlMainServer.cpp`: replace `bot->on_log(dpp::utility::cout_logger());`
  with `bot->on_log(logging::dppLog);`.
- `CMakeLists.txt`: add `Helpers/Log.h Helpers/Log.cpp`.
- `.gitignore`: add `logs/`.
- `docs/` stays untracked; do not add it.

## Commits (branch `file-logger` off master)

1. `Added a file logger` — Log.h/.cpp, the Qt handler and dpp bridge, the
   sink with retention, main/CtrlMainServer/CMake/.gitignore wiring.
2. `Named the bot's threads in the log` — `nameThisThread` at the five call
   sites.

House rules: comments only where really necessary and really short (the why,
never the what); commit messages as a short header plus `*` bullets
(added/deleted/improved/fixed), one line each, ending with the co-author
trailer naming the coding model; nothing pushed.

## Verification

1. Build: the owner's bot is not running now, so the link must succeed
   (no LNK1168 excuse this time). Zero warnings.
2. A throwaway harness in the scratchpad (not the repo) that links
   `Helpers/Log.cpp`: calls `install()`, names the main thread, logs from
   main and from a second named thread and from an unnamed thread, then
   reads the file back and checks: the first line names the path; every
   line matches the format; the named threads appear by name and the
   unnamed one by number; then create 25 dummy `logs/discord_bot-*.log`
   files and call the retention step (or `install()` again in a fresh
   process) and confirm only 20 remain. Because `Log.cpp` references
   `dpp::utility::loglevel`, the harness links against the debug dpp.lib
   at `C:\workspace\libdpp-10.1.6-win64-debug-vs2022\lib\dpp-10.1\dpp.lib`
   with `/MDd` and `Qt6Cored.lib`, and needs `dpp.dll` (same folder's `bin`)
   and `C:\Qt\6.10.2\msvc2022_64\bin` on PATH to run. Report its output.
3. Runtime check is the owner's: start the bot, confirm the `Logging to`
   line, open the file while it runs, Ctrl+C, confirm the last lines are
   there.
