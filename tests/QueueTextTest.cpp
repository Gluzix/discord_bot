#include "QueueText.h"
#include "Check.h"

#include <string>
#include <vector>

using LoopMode = PlaybackController::LoopMode;

static std::string repeat(const std::string &unit, size_t count)
{
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        text += unit;
    }
    return text;
}

// count labels, each size times the letter a.
static std::vector<std::string> labels(size_t count, size_t size)
{
    return std::vector<std::string>(count, std::string(size, 'a'));
}

// Nothing playing, the given songs waiting.
static PlaybackController::QueueSnapshot waiting(const std::vector<std::string> &queued)
{
    PlaybackController::QueueSnapshot snapshot;
    snapshot.queued = queued;
    return snapshot;
}

// The first count waiting songs, numbered as /queue numbers them.
static std::string lines(const std::vector<std::string> &queued, size_t count)
{
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        text += std::to_string(i + 1) + ". " + queued[i] + "\n";
    }
    return text;
}

int main()
{
    { // a song playing, looping the queue
        PlaybackController::QueueSnapshot snapshot;
        snapshot.current = "Song";
        snapshot.queued = {"One", "Two", "Three"};
        snapshot.loop = LoopMode::Queue;
        check(queuetext::render(snapshot) == "Now playing: **Song** (loop: queue)\n1. One\n2. Two\n3. Three\n",
              "loop queue: the playing song with its loop, then every song");
    }

    { // a song playing, loop off
        PlaybackController::QueueSnapshot snapshot;
        snapshot.current = "Song";
        snapshot.queued = {"One"};
        check(queuetext::render(snapshot) == "Now playing: **Song**\n1. One\n", "loop off: the playing song alone, then the song");
    }

    { // 15 short songs
        const std::vector<std::string> queued = labels(15, 10);
        check(queuetext::render(waiting(queued)) == lines(queued, 15), "15 short songs: all of them, no last line");
    }

    { // 16 short songs
        const std::vector<std::string> queued = labels(16, 10);
        check(queuetext::render(waiting(queued)) == lines(queued, 15) + "...and 1 more\n",
              "16 short songs: 15 of them, then ...and 1 more");
    }

    { // 100-character titles with their links
        const std::vector<std::string> queued = labels(20, 149);
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 12) + "...and 8 more\n", "20 labels of 149: 12 of them, then ...and 8 more");
        check(text.size() == 1853, "20 labels of 149: 1853 characters");
    }

    { // the same, with such a song playing on loop
        const std::vector<std::string> queued = labels(20, 149);
        PlaybackController::QueueSnapshot snapshot = waiting(queued);
        snapshot.current = std::string(149, 'a');
        snapshot.loop = LoopMode::Song;
        const std::string text = queuetext::render(snapshot);
        check(text == "Now playing: **" + snapshot.current + "** (loop: song)\n" + lines(queued, 11) + "...and 9 more\n",
              "playing 149 and 20 of 149: 11 of them, then ...and 9 more");
        check(text.size() == 1879, "playing 149 and 20 of 149: 1879 characters");
    }

    { // long links
        const std::vector<std::string> queued = labels(15, 252);
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 7) + "...and 8 more\n", "15 labels of 252: 7 of them, then ...and 8 more");
        check(text.size() == 1806, "15 labels of 252: 1806 characters");
    }

    { // 15 songs and the last line at exactly 2000
        const std::vector<std::string> queued = labels(24, 128);
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 15) + "...and 9 more\n", "24 labels of 128: 15 of them, then ...and 9 more");
        check(text.size() == 2000, "24 labels of 128: exactly 2000 characters");
    }

    { // the same, one character more in the 15th
        std::vector<std::string> queued = labels(24, 128);
        queued[14] = std::string(129, 'a');
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 14) + "...and 10 more\n", "the 15th at 129: 14 of them, then ...and 10 more");
        check(text.size() == 1868, "the 15th at 129: 1868 characters");
    }

    { // the 15th fits, but not with ...and 10 more
        const std::vector<std::string> queued = labels(25, 128);
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 14) + "...and 11 more\n", "25 labels of 128: 14 of them, then ...and 11 more");
        check(text.size() == 1868, "25 labels of 128: 1868 characters");
    }

    { // two songs at exactly 2000
        const std::vector<std::string> queued = labels(2, 996);
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 2), "2 labels of 996: both, no last line");
        check(text.size() == 2000, "2 labels of 996: exactly 2000 characters");
    }

    { // the same, one character more in the second
        std::vector<std::string> queued = labels(2, 996);
        queued[1] = std::string(997, 'a');
        const std::string text = queuetext::render(waiting(queued));
        check(text == lines(queued, 1) + "...and 1 more\n", "the second at 997: the first, then ...and 1 more");
        check(text.size() == 1014, "the second at 997: 1014 characters");
    }

    { // a first song too long for any message
        const std::vector<std::string> queued = {std::string(2001, 'a'), std::string(10, 'a')};
        check(queuetext::render(waiting(queued)) == "...and 2 more\n", "a label of 2001 first: ...and 2 more alone");
    }

    { // a first song that fits, but not with ...and 1 more
        const std::vector<std::string> queued = {std::string(1983, 'a'), std::string(10, 'a')};
        check(queuetext::render(waiting(queued)) == "...and 2 more\n", "a label of 1983 first: ...and 2 more alone");
    }

    { // Cyrillic titles: 2 bytes, 1 UTF-16 unit each
        const std::string label = "[" + repeat("\xD0\xB0", 60) + "](<https://www.youtube.com/watch?v=abcdefghijk>)";
        PlaybackController::QueueSnapshot snapshot = waiting(std::vector<std::string>(15, label));
        snapshot.current = label;
        snapshot.loop = LoopMode::Queue;
        const std::string text = queuetext::render(snapshot);
        check(text == "Now playing: **" + label + "** (loop: queue)\n" + lines(snapshot.queued, 15),
              "Cyrillic titles: the playing song and all 15, no last line");
        check(text.size() == 2802, "Cyrillic titles: 2802 bytes");
    }

    const std::string note = "\xF0\x9F\x8E\xB5";

    { // notes: 4 bytes, 2 UTF-16 units each
        const std::vector<std::string> queued = {repeat(note, 498), repeat(note, 498)};
        check(queuetext::render(waiting(queued)) == lines(queued, 2), "2 labels of 498 notes: both, no last line");
    }

    { // the same, one note more in the second
        const std::vector<std::string> queued = {repeat(note, 498), repeat(note, 499)};
        check(queuetext::render(waiting(queued)) == lines(queued, 1) + "...and 1 more\n",
              "the second at 499 notes: the first, then ...and 1 more");
    }

    return summary();
}
