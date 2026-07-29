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
