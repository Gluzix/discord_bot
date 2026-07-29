#include <windows.h>
#include <string>

#pragma once

class WindowsProcessRunner
{
public:
    WindowsProcessRunner(const std::string &url, HANDLE& outReadHandle);
    void launchYtDlp(); //virtual or template?

private:
    std::string mUrl;
    HANDLE &mOutReadHandle;
    HANDLE mReadPipe, mWritePipe;
};
