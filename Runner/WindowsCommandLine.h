#pragma once

#include <string>
#include <vector>

namespace windows {

// One command line from a program and its arguments, quoted so that the
// program started reads back exactly these arguments.
std::string commandLine(const std::string &program, const std::vector<std::string> &arguments);

}
