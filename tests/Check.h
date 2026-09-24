#pragma once

#include <cstdio>

inline int failures = 0;

inline void check(bool condition, const char *what)
{
    std::printf("%-66s %s\n", what, condition ? "ok" : "FAILED");
    if (!condition) {
        ++failures;
    }
}

inline int summary()
{
    std::printf("%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
