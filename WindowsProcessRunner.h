// Keep windows.h from dragging in the old winsock.h, which conflicts with
// the WinSock2.h that dpp includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

#pragma once

struct ResolvedMedia
{
    std::string title;
    std::string directUrl; // empty on failure
};

class WindowsProcessRunner
{
public:
    WindowsProcessRunner(const std::string &url, HANDLE& outReadHandle);
    void launchYtDlp(); //virtual or template?

    // Runs yt-dlp once to resolve both the video title and the direct media
    // URL. directUrl is empty on failure.
    static ResolvedMedia resolveMedia(const std::string &youtubeUrl);

private:
    std::string mUrl;
    HANDLE &mOutReadHandle;
    HANDLE mReadPipe, mWritePipe;
};
