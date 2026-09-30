#include "Log.h"
#include "Check.h"

#include <dpp/dpp.h>

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <crtdbg.h>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static const std::uintmax_t MIB = 1024 * 1024;

static std::filesystem::path inLogs(const std::string &name)
{
    return std::filesystem::path("logs") / name;
}

static void seed(const std::string &name, std::uintmax_t bytes)
{
    std::ofstream(inLogs(name)).close();
    std::error_code error;
    std::filesystem::resize_file(inLogs(name), bytes, error);
}

static std::vector<std::string> logNames()
{
    std::vector<std::string> names;
    std::error_code error;
    std::filesystem::directory_iterator entry("logs", error);
    for (; !error && entry != std::filesystem::directory_iterator(); entry.increment(error)) {
        names.push_back(entry->path().filename().u8string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Windows brings the size in a listing up to date only when the file is closed.
static std::uintmax_t sizeOf(const std::string &name)
{
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(inLogs(name), error);
    return error ? 0 : bytes;
}

static std::vector<std::string> lines(const std::string &name)
{
    std::vector<std::string> found;
    std::ifstream file(inLogs(name));
    for (std::string line; std::getline(file, line);) {
        found.push_back(line);
    }
    return found;
}

static std::string firstLine(const std::string &name)
{
    const std::vector<std::string> all = lines(name);
    return all.empty() ? "" : all.front();
}

static std::string lastLine(const std::string &name)
{
    const std::vector<std::string> all = lines(name);
    return all.empty() ? "" : all.back();
}

static bool isLogLine(const std::string &line, const std::string &afterTime)
{
    static const std::regex time(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} )");
    return line.size() > 24 && std::regex_match(line.substr(0, 24), time) && line.substr(24) == afterTime;
}

static std::string header(const std::string &name)
{
    std::error_code error;
    return "[main] INFO: Logging to " + std::filesystem::absolute(inLogs(name), error).u8string();
}

static void trace(const std::string &message)
{
    dpp::log_t event;
    event.severity = dpp::ll_trace;
    event.message = message;
    logging::dppLog(event);
}

int main()
{
    // An assertion or an abort must not wait for a click: ctest has no time limit here.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    logging::nameThisThread("main");

    // A name outside the code page; without u8path it would be read in the code page.
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(error) / std::filesystem::u8path(u8"discord_bot_LogTest_\u0416\u0443\u043A");
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "logs", error);
    std::filesystem::current_path(directory, error);
    check(!error, "setup: a fresh directory of its own is the current one");
    if (error) {
        return summary();
    }

    seed("discord_bot-20000101-000000.log", 10 * MIB);
    seed("discord_bot-20000102-000000.log", 20 * MIB);
    seed("discord_bot-20000103-000000.log", 25 * MIB);
    seed("bot.log", 1 * MIB);

    try {
        logging::install();
    } catch (const std::exception &) {
        check(false, "install: no exception");
        return summary();
    }
    const std::time_t installed = std::time(nullptr);
    const std::vector<std::string> afterInstall = logNames();
    const std::string first = afterInstall.empty() ? "" : afterInstall.back();

    { // install() opens a file of its own and makes room for it
        check(std::regex_match(first, std::regex(R"(discord_bot-\d{8}-\d{6}\.log)")),
              "install: a new file named discord_bot-<date>-<time>.log");
        check(afterInstall == std::vector<std::string>{"bot.log", "discord_bot-20000102-000000.log",
                                                       "discord_bot-20000103-000000.log", first},
              "install: past 50 MB the oldest goes, bot.log is no log file");
        check(isLogLine(firstLine(first), header(first)), "install: the first line says where the log goes");
    }

    { // a line from Qt
        qDebug("a line from Qt");
        check(isLogLine(lastLine(first), "[main] DEBUG: a line from Qt"),
              "qDebug: in the file with its time, thread and level");
    }

    { // dpp's websocket trace
        std::ostringstream console;
        std::streambuf *out = std::cout.rdbuf(console.rdbuf());
        std::streambuf *err = std::cerr.rdbuf(console.rdbuf());
        trace("a websocket frame");
        std::cout.rdbuf(out);
        std::cerr.rdbuf(err);
        check(isLogLine(lastLine(first), "[main] TRACE: a websocket frame"), "dpp TRACE: in the file");
        check(console.str().empty(), "dpp TRACE: not on the console");
    }

    { // a file that reaches 10 MB
        // Files are named to the second: one that fills within it is opened again under its own name.
        while (std::time(nullptr) == installed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const std::string frame(1024, 'x');
        for (int written = 0; written < 11 * 1024 && sizeOf(first) < 10 * MIB; ++written) {
            trace(frame);
        }
        const std::vector<std::string> afterFill = logNames();
        const std::string second = afterFill.empty() ? "" : afterFill.back();
        check(second != first && sizeOf(first) >= 10 * MIB && sizeOf(first) < 10 * MIB + 2048,
              "10 MB: the full file ends with the line that crossed");
        check(lines(second).size() == 1 && isLogLine(firstLine(second), header(second)),
              "10 MB: the fresh file holds its header line only");
        check(afterFill == std::vector<std::string>{"bot.log", "discord_bot-20000103-000000.log", first, second},
              "10 MB: past 50 MB again the oldest goes");
    }

    std::filesystem::current_path(directory.parent_path(), error);
    // Windows keeps the file Log still holds open, and so its directories, until the next run.
    std::filesystem::remove_all(directory, error);
    return summary();
}
