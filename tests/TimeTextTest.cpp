#include "TimeText.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void expectParsed(const std::string &text, int seconds)
{
    const std::optional<int> got = timetext::parsePosition(text);
    const bool ok = got.has_value() && *got == seconds;
    failures += ok ? 0 : 1;
    std::printf("parse %-10s -> %-8s (want %d) %s\n", ("\"" + text + "\"").c_str(),
                got ? std::to_string(*got).c_str() : "rejected", seconds, ok ? "PASS" : "FAIL");
}

void expectRejected(const std::string &text)
{
    const std::optional<int> got = timetext::parsePosition(text);
    const bool ok = !got.has_value();
    failures += ok ? 0 : 1;
    std::printf("parse %-10s -> %-8s (want rejected) %s\n", ("\"" + text + "\"").c_str(),
                got ? std::to_string(*got).c_str() : "rejected", ok ? "PASS" : "FAIL");
}

void expectFormatted(double seconds, const std::string &text)
{
    const std::string got = timetext::formatPosition(seconds);
    const bool ok = got == text;
    failures += ok ? 0 : 1;
    std::printf("format %-8.0f -> %-8s (want %s) %s\n", seconds, got.c_str(), text.c_str(),
                ok ? "PASS" : "FAIL");
}

void expectProgress(double position, double duration, const std::string &text)
{
    const std::string got = timetext::formatProgress(position, duration);
    const bool ok = got == text;
    failures += ok ? 0 : 1;
    std::printf("progress %.0f/%-5.0f -> %-12s (want %s) %s\n", position, duration, got.c_str(),
                text.c_str(), ok ? "PASS" : "FAIL");
}

} // namespace

int main()
{
    expectParsed("90", 90);
    expectParsed("1:30", 90);
    expectParsed("1:02:03", 3723);
    expectParsed("0", 0);
    expectRejected("abc");
    expectRejected("1:75");
    expectRejected("-5");
    expectRejected("1::3");
    expectRejected("");
    expectRejected(":30");
    expectRejected("1:30:");
    expectRejected("1:2:3:4");
    expectRejected("1 30");

    expectFormatted(95, "1:35");
    expectFormatted(3723, "1:02:03");
    expectFormatted(5, "0:05");
    expectFormatted(0, "0:00");
    expectFormatted(-3, "0:00");
    expectFormatted(59.6, "1:00");

    expectProgress(155, 250, "2:35 / 4:10");
    expectProgress(155, 0, "2:35");

    std::printf("\n%s: %d check(s) failed\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
