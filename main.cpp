#include "CtrlMainServer.h"
#include "Log.h"

#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>

// DPP 10.1.6+ verifies TLS certificates through OpenSSL, which has no default
// CA store on Windows - without one, every connection fails with "Malformed
// HTTP response". Point OpenSSL at the bundled Mozilla CA file (copied next
// to the exe by the build).
//
// The twist: libcrypto is a release binary, so its getenv() reads the release
// CRT's (ucrtbase.dll) environment cache - which a debug exe's _putenv_s
// never touches. Set the variable inside that CRT instance directly.
static bool environmentVariableIsSet(const char* name)
{
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0) {
        return false;
    }
    bool isSet = (value != nullptr);
    free(value);
    return isSet;
}

static void pointOpenSslAtBundledCertificates()
{
    if (environmentVariableIsSet("SSL_CERT_FILE")) {
        return; // user configured their own bundle
    }

    _putenv_s("SSL_CERT_FILE", "cacert.pem");

    typedef int (__cdecl *PutEnvFn)(const char*, const char*);
    if (HMODULE releaseCrt = GetModuleHandleA("ucrtbase.dll")) {
        if (auto putEnvInReleaseCrt = (PutEnvFn)GetProcAddress(releaseCrt, "_putenv_s")) {
            putEnvInReleaseCrt("SSL_CERT_FILE", "cacert.pem");
        }
    }
}

// Ctrl+C's default behavior is an instant ExitProcess - no destructors, no
// thread joins, reported as a crash. This handler routes it into a clean
// dpp shutdown instead: run() returns and everything unwinds normally.
static CtrlMainServer* activeServer = nullptr;

static BOOL WINAPI consoleCtrlHandler(DWORD signalType)
{
    switch (signalType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        if (activeServer != nullptr) {
            activeServer->requestShutdown();
        }
        return TRUE;
    default:
        return FALSE;
    }
}

int main(int argc, char *argv[])
{
    logging::install();

    // dpp paces voice packets with short sleeps on its socket thread. At
    // Windows' default 15.6ms timer granularity they overshoot enough to
    // stutter or to spin its pacing loop past a 16-bit counter (the crash).
    timeBeginPeriod(1);
    pointOpenSslAtBundledCertificates();

    CtrlMainServer server;
    activeServer = &server;
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    server.run();

    SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
    activeServer = nullptr;
    timeEndPeriod(1);

    return 0;
}
