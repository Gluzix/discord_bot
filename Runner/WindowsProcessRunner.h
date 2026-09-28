#pragma once

#include "IProcessRunner.h"

// Runs a program with Win32 and hands back its output.
// =======================================================
// Rules:
// - run() puts every run in a job object: a kill must also reach what the
//   program spawns (yt-dlp's PyInstaller child interpreter, a JS runtime).
// - run() bounds every run: the pipe is polled with PeekNamedPipe, never
//   read blocking, so a cancel or the timeout can end it at any moment.
// - In run(), the command line goes through CreateProcessW as UTF-16:
//   the A variant would mangle non-ASCII search queries through the ANSI code
//   page.
// - Piped output can come in the ANSI code page (cp1250 here), and yt-dlp's
//   sometimes does even with --encoding utf-8, which Discord shows as
//   mojibake: run() puts every line through ensureUtf8().
// =======================================================
class WindowsProcessRunner : public IProcessRunner
{
public:
    Output run(const std::string &program, const std::vector<std::string> &arguments,
               std::chrono::seconds timeout, const CancelCheck &cancelled) override;

private:
    static const int PIPE_READ_CHUNK = 4096;
    static const int PIPE_BUFFER_BYTES = 64 * 1024;
    static const int POLL_INTERVAL_MS = 50;
};
