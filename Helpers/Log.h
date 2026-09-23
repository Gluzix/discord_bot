#pragma once

// Only Log.cpp pulls dpp in - everyone else just needs the handler's signature.
namespace dpp { struct log_t; }

// Every line the bot prints, timestamped and kept in a file.
// =======================================================
// Rules:
// - writeLine() runs on every thread there is and holds sinkMutex, so it
//   must never log itself.
// - openLogFile() runs with sinkMutex held - install() and rotation inside
//   writeLine() call it - so it writes its header straight to the stream:
//   writeLine() would deadlock on that same mutex.
// - sinkMutex() and logFile() never destroy the sink's mutex and file: a dpp
//   thread can still log while the statics unwind.
// - dpp's TRACE lines are its websocket frame dump: worth keeping, too loud
//   to show, so dppLog() sends them to the file only.
// =======================================================
namespace logging {

// Routes every qDebug/qWarning/qCritical and every dpp log line into
// logs/discord_bot-<start time>.log (working directory) and still to the
// console. Call once at the top of main(), after nameThisThread so the first
// line carries the name, and before anything logs.
void install();

// Names the calling thread for the log and for the debugger.
void nameThisThread(const char *name);

// The dpp on_log handler: bot->on_log(logging::dppLog);
void dppLog(const dpp::log_t &event);

}
