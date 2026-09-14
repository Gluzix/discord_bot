#include "WindowsProcessRunner.h"

// Keep windows.h from dragging in the old winsock.h, which conflicts with
// the WinSock2.h that dpp includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QDebug>
#include <chrono>
#include <cstdlib>
#include <vector>

const std::string WindowsProcessRunner::YT_DLP = "yt-dlp";
const std::string WindowsProcessRunner::YT_DLP_PATH = "C:/Users/kamil/Downloads/ytdlp/yt-dlp.exe";
const std::string WindowsProcessRunner::YT_DLP_SONG_ARGS = "--no-playlist --no-warnings --socket-timeout 10 --encoding utf-8 -f bestaudio --print title --print webpage_url --print urls";
const std::string WindowsProcessRunner::YT_DLP_SEARCH_ARGS = "--flat-playlist --no-warnings --socket-timeout 10 --encoding utf-8 --print \"%(ie_key)s %(url)s\"";
const std::string WindowsProcessRunner::SEARCH_PREFIX = "ytsearch1:";

static bool isValidUtf8(const std::string &text)
{
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = text[i];
        int continuationBytes =
            (c <= 0x7F) ? 0 :
            ((c & 0xE0) == 0xC0) ? 1 :
            ((c & 0xF0) == 0xE0) ? 2 :
            ((c & 0xF8) == 0xF0) ? 3 : -1;
        if (continuationBytes < 0 || i + continuationBytes >= text.size()) {
            return false;
        }
        for (int k = 1; k <= continuationBytes; ++k) {
            if ((text[i + k] & 0xC0) != 0x80) {
                return false;
            }
        }
        i += continuationBytes + 1;
    }
    return true;
}

static std::string ansiToUtf8(const std::string &text)
{
    int wideLen = MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), nullptr, 0);
    if (wideLen <= 0) {
        return text;
    }
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_ACP, 0, text.data(), (int)text.size(), wide.data(), wideLen);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        return text;
    }
    std::string utf8(utf8Len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLen, utf8.data(), utf8Len, nullptr, nullptr);
    return utf8;
}

static std::wstring utf8ToWide(const std::string &text)
{
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    if (wideLen <= 0) {
        return {};
    }
    std::wstring wide(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), wideLen);
    return wide;
}

// yt-dlp sometimes ignores --encoding utf-8 and writes the ANSI code page
// (cp1250 here); convert rather than hand Discord bytes it renders as U+FFFD.
static std::string ensureUtf8(const std::string &text)
{
    if (!text.empty() && !isValidUtf8(text)) {
        return ansiToUtf8(text);
    }
    return text;
}

WindowsProcessRunner::YtDlpOutput WindowsProcessRunner::runYtDlp(const std::string &args, const CancelCheck &cancelled)
{
    YtDlpOutput result;

    SECURITY_ATTRIBUTES secAttr{};
    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    secAttr.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &secAttr, PIPE_BUFFER_BYTES)) {
        qDebug() << "Failed to create yt-dlp pipes";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // A kill must also reach what yt-dlp spawns (the PyInstaller child
    // interpreter, a JS runtime) - a job object takes down the whole tree.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // The command line goes through CreateProcessW as UTF-16 - the A variant
    // would mangle non-ASCII search queries through the ANSI code page.
    // Suspended until it is inside the job, so nothing can be spawned outside it.
    const DWORD creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    PROCESS_INFORMATION processInfo{};
    std::wstring commandToRun = utf8ToWide(YT_DLP + " " + args);
    if (!CreateProcessW(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, creationFlags, nullptr, nullptr, &startupInfo, &processInfo)) {
        // yt-dlp not on PATH - retry with the known local install.
        commandToRun = utf8ToWide("\"" + YT_DLP_PATH + "\" " + args);
        if (!CreateProcessW(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, creationFlags, nullptr, nullptr, &startupInfo, &processInfo)) {
            qDebug() << "Failed to create yt-dlp process";
            CloseHandle(job);
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            return result;
        }
    }
    if (!AssignProcessToJobObject(job, processInfo.hProcess)) {
        qDebug() << "yt-dlp runs outside a job object - a kill won't reach its children";
    }
    ResumeThread(processInfo.hThread);
    CloseHandle(processInfo.hThread);

    // Parent must close its copy of the write end or the pipe never drains.
    CloseHandle(writePipe);

    std::string output;
    char buffer[PIPE_READ_CHUNK];
    auto drainPipe = [&] {
        DWORD available = 0;
        DWORD bytesRead = 0;
        while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0
               && ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            output.append(buffer, bytesRead);
        }
    };

    // Polled, not blocked: a skip (cancelled) or a runaway extraction
    // (deadline) has to be able to end the run at any moment.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(YT_DLP_TIMEOUT_SECONDS);
    bool killed = false;
    for (;;) {
        drainPipe();
        if (WaitForSingleObject(processInfo.hProcess, POLL_INTERVAL_MS) == WAIT_OBJECT_0) {
            drainPipe();
            break;
        }
        result.cancelled = cancelled && cancelled();
        if (result.cancelled || std::chrono::steady_clock::now() >= deadline) {
            if (!result.cancelled) {
                qDebug() << "yt-dlp killed after" << YT_DLP_TIMEOUT_SECONDS << "s:" << QString::fromStdString(args).right(60);
            }
            TerminateJobObject(job, 1);
            killed = true;
            break;
        }
    }
    CloseHandle(readPipe);

    DWORD exitCode = 1;
    if (!killed) {
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
    }
    result.exitCode = exitCode;
    CloseHandle(processInfo.hProcess);
    CloseHandle(job);

    size_t start = 0;
    while (start < output.size()) {
        size_t eol = output.find('\n', start);
        if (eol == std::string::npos) {
            eol = output.size();
        }
        std::string line = output.substr(start, eol - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            result.lines.push_back(line);
        }
        start = eol + 1;
    }
    return result;
}

// A band-name search often ranks the artist's channel first, and handing
// that to "ytsearch1:" makes yt-dlp extract every upload on it (minutes,
// with the title of the first and the url of the last). So pick the video
// ourselves from a flat listing of the top results.
std::string WindowsProcessRunner::firstVideoUrl(const std::string &query, const CancelCheck &cancelled)
{
    YtDlpOutput run = runYtDlp(YT_DLP_SEARCH_ARGS + " \"ytsearch5:" + query + "\"", cancelled);

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

ResolvedMedia WindowsProcessRunner::resolveMedia(const std::string &target, const CancelCheck &cancelled)
{
    std::string url = target;
    if (target.rfind(SEARCH_PREFIX, 0) == 0) {
        url = firstVideoUrl(target.substr(SEARCH_PREFIX.size()), cancelled);
        if (url.empty()) {
            return {};
        }
    }

    // The --print fields make yt-dlp emit the title, the page url and the
    // direct media URL on consecutive lines, in one process. Without
    // --encoding utf-8, yt-dlp writes pipe output in the ANSI code page
    // (cp1250 here), which turns Polish titles into mojibake on Discord.
    YtDlpOutput run = runYtDlp(YT_DLP_SONG_ARGS + " \"" + url + "\"", cancelled);
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
        media.title = ensureUtf8(lines[0]);
        media.webpageUrl = lines[1];
    } else if (lines.size() == 2) {
        media.title = ensureUtf8(lines[0]);
    }
    return media;
}

PlaylistListing WindowsProcessRunner::listPlaylist(const std::string &playlistUrl, size_t maxEntries)
{
    // --flat-playlist lists entries without extracting any video, so even a
    // 6000-video playlist answers in a few seconds. Each entry prints as a
    // url/title line pair; the "playlist:" prints come once, after them.
    const std::string args = "--flat-playlist --no-warnings --socket-timeout 10 --encoding utf-8"
        " --playlist-items :" + std::to_string(maxEntries) +
        " --print url --print title"
        " --print \"playlist:PLAYLIST_TITLE=%(title)s\""
        " --print \"playlist:PLAYLIST_COUNT=%(playlist_count)s\""
        " \"" + playlistUrl + "\"";
    YtDlpOutput run = runYtDlp(args, {});
    if (run.exitCode != 0) {
        qDebug() << "yt-dlp playlist listing exit code:" << run.exitCode;
    }

    const std::string titleMarker = "PLAYLIST_TITLE=";
    const std::string countMarker = "PLAYLIST_COUNT=";
    PlaylistListing listing;
    std::string pendingUrl;
    for (const std::string &line : run.lines) {
        if (line.rfind(titleMarker, 0) == 0) {
            std::string title = line.substr(titleMarker.size());
            listing.title = (title == "NA") ? std::string{} : ensureUtf8(title);
        } else if (line.rfind(countMarker, 0) == 0) {
            listing.totalCount = std::strtoul(line.c_str() + countMarker.size(), nullptr, 10); // "NA" -> 0
        } else if (pendingUrl.empty()) {
            pendingUrl = line;
        } else {
            // YouTube keeps placeholder entries for videos nobody can play.
            if (line != "[Private video]" && line != "[Deleted video]") {
                listing.entries.push_back({pendingUrl, ensureUtf8(line)});
            }
            pendingUrl.clear();
        }
    }
    return listing;
}
