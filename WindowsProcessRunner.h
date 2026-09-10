// Keep windows.h from dragging in the old winsock.h, which conflicts with
// the WinSock2.h that dpp includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

#pragma once

class WindowsProcessRunner
{
public:
    WindowsProcessRunner(const std::string &url, HANDLE& outReadHandle);
    void launchYtDlp(); //virtual or template?

    // Runs "yt-dlp -g <url>" and returns the direct media URL it prints,
    // or an empty string on failure.
    static std::string resolveDirectUrl(const std::string &youtubeUrl);

private:
    std::string mUrl;
    HANDLE &mOutReadHandle;
    HANDLE mReadPipe, mWritePipe;
};
