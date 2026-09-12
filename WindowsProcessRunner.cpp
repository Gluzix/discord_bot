#include "WindowsProcessRunner.h"

// Keep windows.h from dragging in the old winsock.h, which conflicts with
// the WinSock2.h that dpp includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QDebug>
#include <cstdlib>
#include <vector>

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

// Fallback for a yt-dlp that ignored --encoding utf-8: text arrives in the
// ANSI code page - convert it instead of handing Discord invalid UTF-8
// (which renders as U+FFFD).
static std::string ensureUtf8(const std::string &text)
{
    if (!text.empty() && !isValidUtf8(text)) {
        return ansiToUtf8(text);
    }
    return text;
}

WindowsProcessRunner::YtDlpOutput WindowsProcessRunner::runYtDlp(const std::string &args)
{
    YtDlpOutput result;

    SECURITY_ATTRIBUTES secAttr{};
    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    secAttr.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &secAttr, 0)) {
        qDebug() << "Failed to create yt-dlp pipes";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // The command line goes through CreateProcessW as UTF-16 - the A variant
    // would mangle non-ASCII search queries through the ANSI code page.
    PROCESS_INFORMATION processInfo{};
    std::wstring commandToRun = utf8ToWide("yt-dlp" + args);
    if (!CreateProcessW(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
        // yt-dlp not on PATH - retry with the known local install.
        commandToRun = utf8ToWide("\"C:/Users/kamil/Downloads/ytdlp/yt-dlp.exe\"" + args);
        if (!CreateProcessW(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
            qDebug() << "Failed to create yt-dlp process";
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            return result;
        }
    }

    // Parent must close its copy of the write end or ReadFile never sees EOF.
    CloseHandle(writePipe);

    std::string output;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer, bytesRead);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(processInfo.hProcess, 30000);
    DWORD exitCode = 1;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    result.exitCode = exitCode;
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

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

ResolvedMedia WindowsProcessRunner::resolveMedia(const std::string &target)
{
    // The --print fields make yt-dlp emit the title, the page url and the
    // direct media URL on consecutive lines, in one process. Without
    // --encoding utf-8, yt-dlp writes pipe output in the ANSI code page
    // (cp1250 here), which turns Polish titles into mojibake on Discord.
    YtDlpOutput run = runYtDlp(" --no-playlist --no-warnings --socket-timeout 10 --encoding utf-8 -f bestaudio --print title --print webpage_url --print urls \"" + target + "\"");
    const std::vector<std::string> &lines = run.lines;

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
    const std::string args = " --flat-playlist --no-warnings --socket-timeout 10 --encoding utf-8"
        " --playlist-items :" + std::to_string(maxEntries) +
        " --print url --print title"
        " --print \"playlist:PLAYLIST_TITLE=%(title)s\""
        " --print \"playlist:PLAYLIST_COUNT=%(playlist_count)s\""
        " \"" + playlistUrl + "\"";
    YtDlpOutput run = runYtDlp(args);
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
