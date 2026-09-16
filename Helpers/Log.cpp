#include "Log.h"

// Keep windows.h from dragging in the old winsock.h, which conflicts with
// the WinSock2.h that dpp includes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dpp/dpp.h>

#include <QString>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace logging {

namespace {

const size_t KEEP_NEWEST = 20;

// Never destroyed: a dpp thread can still log while the statics unwind.
std::mutex &sinkMutex()
{
    static std::mutex *mutex = new std::mutex();
    return *mutex;
}

std::ofstream &logFile()
{
    static std::ofstream *file = new std::ofstream();
    return *file;
}

thread_local std::string threadName;

std::string formatLocalTime(std::time_t when, const char *format)
{
    std::tm local{};
    localtime_s(&local, &when);

    char text[32];
    std::strftime(text, sizeof(text), format, &local);
    return text;
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char text[40];
    std::snprintf(text, sizeof(text), "%s.%03d",
                  formatLocalTime(std::chrono::system_clock::to_time_t(now), "%Y-%m-%d %H:%M:%S").c_str(),
                  static_cast<int>(milliseconds.count()));
    return text;
}

std::string threadLabel()
{
    if (!threadName.empty()) {
        return threadName;
    }
    return std::to_string(GetCurrentThreadId());
}

// The one writer for both sources; it runs on every thread there is, and it
// must never log itself.
void writeLine(const std::string &level, const std::string &message, std::ostream &console)
{
    const std::string line = timestamp() + " [" + threadLabel() + "] " + level + ": " + message;

    std::lock_guard<std::mutex> lock(sinkMutex());
    console << line << std::endl;
    if (logFile().is_open()) {
        logFile() << line << std::endl;
    }
}

const char *levelFor(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "DEBUG";
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    writeLine(levelFor(type), message.toStdString(), std::cerr);
}

void pruneOldLogs(const std::filesystem::path &directory)
{
    std::error_code error;
    std::vector<std::filesystem::path> logs;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("discord_bot-", 0) == 0 && entry.path().extension() == ".log") {
            logs.push_back(entry.path());
        }
    }

    if (logs.size() <= KEEP_NEWEST) {
        return;
    }
    // The start time in the name sorts oldest first.
    std::sort(logs.begin(), logs.end());
    for (size_t i = 0; i + KEEP_NEWEST < logs.size(); ++i) {
        std::filesystem::remove(logs[i], error);
    }
}

}

void install()
{
    const std::filesystem::path directory = "logs";
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    const std::filesystem::path path =
        directory / ("discord_bot-" + formatLocalTime(std::time(nullptr), "%Y%m%d-%H%M%S") + ".log");
    logFile().open(path, std::ios::out | std::ios::app);

    qInstallMessageHandler(qtMessageHandler);

    // A log file that won't open is never a reason to stop the bot.
    if (!logFile().is_open()) {
        std::cerr << "Cannot write " << path.string() << " - logging to the console only" << std::endl;
        return;
    }

    pruneOldLogs(directory);
    writeLine("INFO", "Logging to " + std::filesystem::absolute(path, error).string(), std::cout);
}

void nameThisThread(const char *name)
{
    threadName = name;

    // SetThreadDescription wants wide characters; thread names are ASCII.
    std::wstring wide;
    for (char letter : threadName) {
        wide.push_back(static_cast<wchar_t>(letter));
    }
    SetThreadDescription(GetCurrentThread(), wide.c_str());
}

void dppLog(const dpp::log_t &event)
{
    writeLine(dpp::utility::loglevel(event.severity), event.message, std::cout);
}

}
