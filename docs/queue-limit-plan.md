# /queue stays under Discord's message limit

Branch `queue-text-limit`, on top of origin/master (bf260cd): the owner
rebased it after `queue-commands` was merged, and his tree is on this
branch. The work happens there. The change to review is `01f9fae..HEAD`.

The plan went through its check on 2026-09-29: the numbers below are
measured with probes, not worked out by hand.

## The problem

Discord refuses a message content of more than 2000 characters. `/queue`
lists up to 15 waiting songs and its comment says it stays "far below" the
limit. It does not. The bot never shortens a title: YouTube allows 100
characters.

| Line | Worst case |
|---|---|
| a resolved song: `NN. [title](<url>)` + newline | title 100 + url 43 + 11 = 154 characters (url 45 for a playlist entry from music.youtube.com that is not resolved yet) |
| an unresolved link: `NN. <url>` + newline | url 250 (`isAllowedUrl`) + 7 = 257 characters |
| an unresolved search: `NN. search: query` + newline | query 150 (`isReasonableSearchQuery`) + 13 = 163 characters |
| the playing song: `Now playing: **label** (loop: queue)` + newline | 32 + label |
| `...and N more` + newline | 13 + the digits of N |

Fifteen resolved songs with 100-character titles are 2301 characters, with
the playing song and the last line about 2500. Fifteen long unresolved
links are 3846. DPP sends such a text as it is (it cuts at 4000 only),
Discord refuses it, DPP logs the error, and the user sees that the
application did not respond: `/queue` answers nothing exactly when the
queue is full.

## The fix

The text is built line by line, and a song is listed only when the text
stays within 2000 with it, the last line included. `MAX_LISTED` stays the
upper bound. A text well under the limit comes out as it does today.
Within a few characters of 2000 a song may give way: the room for the last
line is kept free before it is known whether that line will be needed.

The length is counted in UTF-16 units, not in bytes. Discord's API counts
code points (its staff in discord-api-docs#1315) and its client counts
UTF-16 units; a text never has fewer UTF-16 units than code points, so
what fits in UTF-16 units fits either way. A byte count would be safe as
well, but it lists 10 of 15 songs with Cyrillic titles that fit today.

## Files

1. `Commands/QueueText.h/.cpp`, modelled on `Commands/NowPlayingLine`. In
   `CMakeLists.txt` a new line `  Commands/QueueText.h Commands/QueueText.cpp`
   right after the `Commands/QueueCommand` line, as `NowPlayingLine`
   follows `NowPlayingCommand`.
   ```cpp
   #pragma once

   #include "PlaybackController.h"

   #include <string>

   // What /queue answers.
   // =======================================================
   // Rules:
   // - Discord refuses a message of more than 2000 characters, and fifteen
   //   songs with long titles or links are more: render() lists a song only
   //   while the text, its "...and N more" line included, stays within 2000.
   // - render() counts UTF-16 units, which are never fewer than the code
   //   points Discord counts; bytes would leave out Cyrillic or Japanese
   //   titles that fit.
   // - render() writes the "Now playing:" line unmeasured: YouTube caps a
   //   title at 100 characters, so one label is a few hundred at most.
   // =======================================================
   namespace queuetext {

   // The playing song, then the waiting ones, as many as fit.
   std::string render(const PlaybackController::QueueSnapshot &snapshot);

   }
   ```
   - The text is what `QueueCommand::execute` builds today, moved here: the
     "Now playing:" line with its loop suffix when a song plays, then
     `<n>. <label>` lines, then `...and N more` when songs are left out.
     Every line ends with a newline, as today.
   - At the top of `QueueText.cpp`, in an unnamed namespace as in
     `Helpers/TimeText.cpp`: `const size_t MAX_LISTED = 15;`,
     `const size_t MAX_LENGTH = 2000;`, the function that counts and the
     function that writes the `...and N more` line. Nothing of it is in the
     header.
   - Counting: over the UTF-8 text, 1 for every byte that starts a
     character (`(c & 0xC0) != 0x80`, `c` an `unsigned char`) and 1 more
     when that byte is `0xF0` or above (such a character takes two UTF-16
     units). Not `dpp::utility::utf8len`: it counts code points and lives
     in dpp.dll, which the test does not link.
   - The rule: waiting song `i` (from 0), in order, while `i < MAX_LISTED`,
     is listed when the length of the text so far, plus the length of its
     line, plus the length of the line `...and N more` with
     `N = queued.size() - (i + 1)` (left out when that `N` is 0), is at most
     `MAX_LENGTH`. Newlines count. The first song that is not listed ends
     the listing; no later, shorter song is pulled forward.
   - After the listing, when songs are left out for whatever reason (past
     `MAX_LISTED`, or no room), the line `...and N more` follows, `N` the
     number of waiting songs that are not listed.
   - The "Now playing:" line goes in unmeasured, and no comment stands
     beside it: the rules block says why.
   - Nothing special for an empty snapshot: `QueueCommand` answers the
     empty queue itself.

2. `Commands/QueueCommand.cpp`: `execute()` keeps the empty-queue answer
   and the mentions comment, and builds its message from
   `queuetext::render(snapshot)`. The old loop, `MAX_LISTED` and the "far
   below" comment go. Includes as `NowPlayingCommand.cpp` orders them:
   `QueueCommand.h`, `QueueText.h`, `PlaybackController.h`, `Messages.h`,
   then `<dpp/dpp.h>`.

3. `tests/QueueTextTest.cpp`, pure like `NowPlayingLineTest`: no DPP, no
   Qt. In `tests/CMakeLists.txt`, each line in the group of its kind:
   ```cmake
   add_bot_test(QueueTextTest QueueTextTest.cpp Check.h ../Commands/QueueText.cpp)
   ```
   right after the `add_bot_test(NowPlayingLineTest ...)` line, and
   ```cmake
   target_include_directories(QueueTextTest PRIVATE "${CMAKE_SOURCE_DIR}/Commands")
   ```
   right after the `target_include_directories(NowPlayingLineTest ...)`
   line. No `target_link_libraries`, no `set_tests_properties`.

   The test compares exact texts, as `NowPlayingLineTest` does. An expected
   text is a fixed number of lines plus the literal last line; the test has
   no loop that works out how many songs fit, and it carries its own 2000
   and 15. A label is `n` times the letter `a` unless said otherwise, so
   `size()` is the length. Cases:

   | Snapshot | Expected text |
   |---|---|
   | playing `Song`, loop queue, waiting `One`, `Two`, `Three` | `Now playing: **Song** (loop: queue)\n1. One\n2. Two\n3. Three\n` |
   | playing `Song`, loop off, waiting `One` | `Now playing: **Song**\n1. One\n` |
   | nothing playing, 15 short songs | all 15, no last line |
   | nothing playing, 16 short songs | songs 1-15, then `...and 1 more` |
   | nothing playing, 20 labels of 149 (a 100-character title with its link) | songs 1-12, then `...and 8 more`; size 1853 |
   | playing a label of 149 with loop song, 20 labels of 149 | the playing line with ` (loop: song)`, songs 1-11, then `...and 9 more`; size 1879 |
   | nothing playing, 15 labels of 252 (long links) | songs 1-7, then `...and 8 more`; size 1806 |
   | nothing playing, 24 labels of 128 | songs 1-15, then `...and 9 more`; size exactly 2000 |
   | the same with the 15th label at 129 | songs 1-14, then `...and 10 more`; size 1868 |
   | nothing playing, 25 labels of 128 (the 15th fits, but not with `...and 10 more`) | songs 1-14, then `...and 11 more`; size 1868 |
   | nothing playing, 2 labels of 996 | both, no last line; size exactly 2000 |
   | the same with the second label at 997 | song 1, then `...and 1 more`; size 1014 |
   | nothing playing, a label of 2001, then a short one | `...and 2 more\n` alone |
   | nothing playing, a label of 1983, then one of 10 (the first fits, but not with `...and 1 more`) | `...and 2 more\n` alone |
   | playing and 15 waiting, loop queue, every label `[` + 60 times `\xD0\xB0` + `](<https://www.youtube.com/watch?v=abcdefghijk>)` | the playing line and all 15, no last line (2802 bytes) |
   | nothing playing, 2 labels of 498 times `\xF0\x9F\x8E\xB5` | both, no last line |
   | the same with the second label at 499 times | song 1, then `...and 1 more` |

   The last three tell the counts apart: bytes list 10 of the Cyrillic
   songs and none of the notes, and code points list both notes in the last
   case. The rows with 25 labels and with the label of 1983 came from the
   review: without them a look-ahead that skips `N = 1`, or that takes the
   last line for 14 characters whatever `N` is, passed every case.

4. `README.md`:
   - the `/queue` row becomes
     `| `/queue` | Shows what's playing and the first 15 waiting songs, fewer when their titles are long. |`;
   - in "How it works", the `Commands/` bullet gets, after its
     `CommandHandler` sentence: "`QueueText` writes the `/queue` answer,
     with as many waiting songs as fit in one message.";
   - the test sentence gets "the `/queue` text, " right after
     "the `/nowplaying` line, ".

## Checked, not changed

Measured in the plan check, in bytes, each far below 2000: `/nowplaying`
(at most about 515), the answer to a button click (565), the `/playlist`
summary (920: a playlist title and a link), the "Playing:" announcement
and the edit that takes its buttons off (460), the "Queued at position N:"
edit (475) and the `/remove` answer (475). Each carries one label, or one
playlist title and one link.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class or namespace header, each bullet naming the function that enforces
it. Code that reads like its surroundings. C++17, as `CMakeLists.txt` sets
it. Commit messages = short header + `*` bullets, one line each, ending
with the coding model's co-author trailer. Nothing pushed. Nothing added or
changed under `docs/`. `token.json` is never read, printed or touched. The
bot is never started, stopped or killed. DPP untouched. Zero warnings.
History is linear: no merge commits. YAGNI: no paging, no option for the
number of songs, no embeds.

One commit, and one more for the two test cases of the review. Files are
staged by name, never with `git add -A` or
`git commit -a`: `CMakeLists.txt`, `README.md`, `Commands/QueueText.h`,
`Commands/QueueText.cpp`, `Commands/QueueCommand.cpp`,
`tests/CMakeLists.txt`, `tests/QueueTextTest.cpp`. After the commit
`git status` shows the three `docs/` entries as before.

## Checks

- Build everything (bot and tests) in the agents' own build directory,
  which is configured on this tree; zero warnings in the ninja output;
  `ctest`: all tests pass, the new one included. Never `cmake -B build` in
  the tree, and never the owner's Qt Creator directory.
- Owner's runtime pass: `/queue` with a few songs looks as before. Queue a
  playlist of songs with long titles, `/queue`: an answer comes, with fewer
  than 15 songs and a last line that counts the rest.
