#include "WindowsCommandLine.h"

namespace {

std::string quotedIfNeeded(const std::string &entry)
{
    if (!entry.empty() && entry.find_first_of(" \t\"") == std::string::npos) {
        return entry;
    }

    // Backslashes escape only a quote, the closing one included.
    std::string quoted = "\"";
    size_t backslashes = 0;
    for (char c : entry) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        quoted.append(c == '"' ? 2 * backslashes + 1 : backslashes, '\\');
        quoted += c;
        backslashes = 0;
    }
    quoted.append(2 * backslashes, '\\');
    return quoted + '"';
}

}

namespace windows {

std::string commandLine(const std::string &program, const std::vector<std::string> &arguments)
{
    std::string line = quotedIfNeeded(program);
    for (const std::string &argument : arguments) {
        line += ' ' + quotedIfNeeded(argument);
    }
    return line;
}

}
