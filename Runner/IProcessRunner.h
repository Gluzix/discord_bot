#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

// Runs a program to its end and hands back what it wrote.
// =======================================================
// Rules:
// - run() is bounded: it returns once the timeout has passed, and within
//   a poll interval once cancelled() says so; what it started is dead by
//   then, with everything that program spawned.
// - The lines run() returns are UTF-8, whatever the program wrote.
// - run() is called from several threads at once.
// =======================================================
class IProcessRunner
{
public:
    // Returns true once the caller no longer wants the result. Empty
    // means never.
    using CancelCheck = std::function<bool()>;

    struct Output
    {
        bool started{false};            // false: the program could not be run at all
        // 0 only when the program ended by itself with 0: never after
        // run() killed it, a crash or a failed start.
        int exitCode{1};
        bool cancelled{false};          // killed because the caller lost interest
        std::vector<std::string> lines; // stdout split into non-empty lines
    };

    virtual ~IProcessRunner() = default;

    virtual Output run(const std::string &program, const std::vector<std::string> &arguments,
                       std::chrono::seconds timeout, const CancelCheck &cancelled) = 0;
};
