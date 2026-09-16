#pragma once

// Only Log.cpp pulls dpp in - everyone else just needs the handler's signature.
namespace dpp { struct log_t; }

// Every line the bot prints, timestamped and kept in a file.
namespace logging {

// Routes every qDebug/qWarning/qCritical and every dpp log line into
// logs/discord_bot-<start time>.log (working directory) and still to the
// console. Call once at the top of main(), before anything logs.
void install();

// Names the calling thread for the log and for the debugger.
void nameThisThread(const char *name);

// The dpp on_log handler: bot->on_log(logging::dppLog);
void dppLog(const dpp::log_t &event);

}
