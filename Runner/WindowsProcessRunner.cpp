#include "WindowsProcessRunner.h"
#include "WindowsCommandLine.h"

// Keep windows.h from dragging in the old winsock.h, which conflicts with
// the WinSock2.h that dpp includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QDebug>
#include <chrono>
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

static std::string ensureUtf8(const std::string &text)
{
    if (!text.empty() && !isValidUtf8(text)) {
        return ansiToUtf8(text);
    }
    return text;
}

IProcessRunner::Output WindowsProcessRunner::run(const std::string &program, const std::vector<std::string> &arguments,
                                                 std::chrono::seconds timeout, const CancelCheck &cancelled)
{
    Output result;
    const QString name = QString::fromStdString(program);

    SECURITY_ATTRIBUTES secAttr{};
    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    secAttr.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &secAttr, PIPE_BUFFER_BYTES)) {
        qDebug().noquote() << "Failed to create the pipes for" << name;
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    // Suspended until it is inside the job, so nothing can be spawned outside it.
    const DWORD creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    PROCESS_INFORMATION processInfo{};
    const std::string commandLine = windows::commandLine(program, arguments);
    std::wstring commandToRun = utf8ToWide(commandLine);
    if (!CreateProcessW(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, creationFlags, nullptr, nullptr, &startupInfo, &processInfo)) {
        const DWORD error = GetLastError(); // before qDebug can overwrite it
        qDebug().noquote() << "Could not start" << name + "," << "error:" << error;
        CloseHandle(job);
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return result;
    }
    result.started = true;
    if (!AssignProcessToJobObject(job, processInfo.hProcess)) {
        qDebug().noquote() << name << "runs outside a job object - a kill won't reach its children";
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

    const auto deadline = std::chrono::steady_clock::now() + timeout;
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
                qDebug().noquote() << name << "killed after" << timeout.count() << "s:"
                                   << QString::fromStdString(commandLine).right(60);
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
    result.exitCode = static_cast<int>(exitCode);
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
            result.lines.push_back(ensureUtf8(line));
        }
        start = eol + 1;
    }
    return result;
}
