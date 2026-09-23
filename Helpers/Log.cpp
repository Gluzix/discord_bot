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
#include <cstdint>
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

const char *LOG_DIRECTORY = "logs";
const std::uintmax_t MAX_FILE_BYTES = 10 * 1024 * 1024;
const std::uintmax_t MAX_TOTAL_BYTES = 50 * 1024 * 1024;

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

std::uintmax_t sizeOf(const std::filesystem::path &path)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

std::vector<std::filesystem::path> logFilesOldestFirst()
{
    std::error_code error;
    std::vector<std::filesystem::path> logs;
    for (const auto &entry : std::filesystem::directory_iterator(LOG_DIRECTORY, error)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("discord_bot-", 0) == 0 && entry.path().extension() == ".log") {
            logs.push_back(entry.path());
        }
    }
    // The start time in the name sorts oldest first.
    std::sort(logs.begin(), logs.end());
    return logs;
}

void pruneOldLogs(const std::filesystem::path &openPath)
{
    const std::vector<std::filesystem::path> logs = logFilesOldestFirst();
    std::uintmax_t total = 0;
    for (const std::filesystem::path &log : logs) {
        total += sizeOf(log);
    }

    std::error_code error;
    for (const std::filesystem::path &log : logs) {
        if (total <= MAX_TOTAL_BYTES) {
            break;
        }
        if (log.filename() == openPath.filename()) {
            continue; // never the file being written
        }
        const std::uintmax_t size = sizeOf(log);
        if (std::filesystem::remove(log, error)) {
            total -= size;
        }
    }
}

void openLogFile()
{
    std::error_code error;
    std::filesystem::create_directories(LOG_DIRECTORY, error);

    const std::filesystem::path path = std::filesystem::path(LOG_DIRECTORY)
        / ("discord_bot-" + formatLocalTime(std::time(nullptr), "%Y%m%d-%H%M%S") + ".log");
    logFile().open(path, std::ios::out | std::ios::app);

    // A log file that won't open is never a reason to stop the bot.
    if (!logFile().is_open()) {
        std::cerr << "Cannot write " << path.string() << " - logging to the console only" << std::endl;
        return;
    }

    const std::string header = timestamp() + " [" + threadLabel() + "] INFO: Logging to "
                               + std::filesystem::absolute(path, error).string();
    std::cout << header << std::endl;
    logFile() << header << std::endl;

    pruneOldLogs(path);
}

// A null console keeps the line out of the terminal.
void writeLine(const std::string &level, const std::string &message, std::ostream *console)
{
    const std::string line = timestamp() + " [" + threadLabel() + "] " + level + ": " + message;

    std::lock_guard<std::mutex> lock(sinkMutex());
    if (console != nullptr) {
        *console << line << std::endl;
    }
    if (!logFile().is_open()) {
        return;
    }
    logFile() << line << std::endl;

    const std::streamoff written = logFile().tellp();
    if (written >= 0 && static_cast<std::uintmax_t>(written) >= MAX_FILE_BYTES) {
        logFile().close();
        openLogFile();
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
    writeLine(levelFor(type), message.toStdString(), &std::cerr);
}

}

void install()
{
    qInstallMessageHandler(qtMessageHandler);

    std::lock_guard<std::mutex> lock(sinkMutex());
    openLogFile();
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
    std::ostream *console = event.severity == dpp::ll_trace ? nullptr : &std::cout;
    writeLine(dpp::utility::loglevel(event.severity), event.message, console);
}

}
