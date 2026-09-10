#include "WindowsProcessRunner.h"
#include <QDebug>

WindowsProcessRunner::WindowsProcessRunner(const std::string &url, HANDLE &outReadHandle)
    : mUrl(url)
    , mOutReadHandle(outReadHandle)
{

}

void WindowsProcessRunner::launchYtDlp()
{
    SECURITY_ATTRIBUTES secAttr{};
    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    secAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&mReadPipe, &mWritePipe, &secAttr, 0)) {
        qDebug() << "Failed to create yt-dlp process pipes";
        return;
    }

    SetHandleInformation(&mReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startupInfoA{};
    startupInfoA.cb = sizeof(STARTUPINFOA);
    startupInfoA.dwFlags |= STARTF_USESTDHANDLES;
    startupInfoA.hStdOutput = mWritePipe;
    startupInfoA.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION processInfo{};
    std::string commandToRun = "yt-dlp -f bestaudio[ext=m4a] -o - \"" + mUrl + "\"";

    if (!CreateProcessA(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startupInfoA, &processInfo)) {
        qDebug() << "Failed to create yt-dlp process";
        return;
    }

    CloseHandle(mWritePipe);
    mOutReadHandle = mReadPipe;

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
}

std::string WindowsProcessRunner::resolveDirectUrl(const std::string &youtubeUrl)
{
    SECURITY_ATTRIBUTES secAttr{};
    secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    secAttr.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &secAttr, 0)) {
        qDebug() << "Failed to create yt-dlp pipes";
        return {};
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOA);
    startupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    const std::string args = " --no-playlist --no-warnings --socket-timeout 10 -f bestaudio -g \"" + youtubeUrl + "\"";

    PROCESS_INFORMATION processInfo{};
    std::string commandToRun = "yt-dlp" + args;
    if (!CreateProcessA(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
        // yt-dlp not on PATH - retry with the known local install.
        commandToRun = "\"C:/Users/kamil/Downloads/ytdlp/yt-dlp.exe\"" + args;
        if (!CreateProcessA(nullptr, commandToRun.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo)) {
            qDebug() << "Failed to create yt-dlp process";
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            return {};
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
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    // First stdout line is the direct media URL.
    size_t eol = output.find_first_of("\r\n");
    std::string url = (eol == std::string::npos) ? output : output.substr(0, eol);

    if (exitCode != 0 || url.rfind("http", 0) != 0) {
        qDebug() << "yt-dlp did not return a usable URL, exit code:" << exitCode;
        return {};
    }
    return url;
}
