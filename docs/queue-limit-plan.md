# /queue stays under Discord's message limit

Status: parked on 2026-09-28, before any code was written. The plan below
has not been through its check yet: the numbers in the table are worked
out by hand from the code, not measured.

Branch `queue-text-limit` off master, in its own worktree (the owner's tree
stays on the branch he is testing).

## The problem

Discord refuses a message content of more than 2000 characters. `/queue`
lists up to 15 waiting songs and its comment says it stays "far below" the
limit. It does not:

| Line | Worst case |
|---|---|
| a resolved song: `NN. [title](<url>)` + newline | title 100 + url 43 + 11 = 154 characters |
| an unresolved link: `NN. <url>` + newline | url 250 (`isAllowedUrl`) + 7 = 257 characters |
| an unresolved search: `NN. search: query` + newline | query 150 (`isReasonableSearchQuery`) + 13 = 163 characters |
| the playing song: `Now playing: **label** (loop: queue)` + newline | 32 + label |
| `...and N more` + newline | 13 + the digits of N |

Fifteen resolved songs with 100-character titles are about 2300
characters, with the playing song and the last line about 2500. Fifteen
long unresolved links are about 3850. The interaction then fails, and
`/queue` answers nothing exactly when the queue is full.

## The fix

The text is built line by line, and a song is listed only when the text
stays within the limit with it, the last line included. `MAX_LISTED` stays
the upper bound. Nothing changes while the text is short.

## Files

1. `Commands/QueueText.h/.cpp` (in `CMakeLists.txt` next to
   `Commands/QueueCommand`), modelled on `Commands/NowPlayingLine`:
   ```cpp
   // What /queue answers.
   // =======================================================
   // Rules:
   // - Discord refuses a message of more than 2000 characters, and fifteen
   //   songs with long titles are more than that: render() lists a song
   //   only while the text, its "...and N more" line included, stays within
   //   MAX_BYTES.
   // - render() counts bytes where Discord counts characters: a text has
   //   never fewer bytes than characters, so what fits in bytes fits.
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
   - `MAX_LISTED = 15` and `MAX_BYTES = 2000` are constants of the `.cpp`.
   - For each waiting song, in order, up to `MAX_LISTED`: the song is
     listed when the text with its line, plus the `...and N more` line that
     would follow if songs remain after it, is at most `MAX_BYTES` long.
     The first song that does not fit ends the listing; no later, shorter
     song is pulled forward.
   - `N` is the number of waiting songs that are not listed, whatever the
     reason (past `MAX_LISTED`, or no room).
   - The "Now playing:" line always goes in: one label is at most a few
     hundred bytes.

2. `Commands/QueueCommand.cpp`: `execute()` keeps the empty-queue answer
   and the mentions comment, and builds its message from
   `queuetext::render(snapshot)`. The old loop and its "far below" comment
   go.

3. `tests/QueueTextTest.cpp`, pure like `NowPlayingLineTest`:
   ```cmake
   add_bot_test(QueueTextTest QueueTextTest.cpp Check.h ../Commands/QueueText.cpp)
   target_include_directories(QueueTextTest PRIVATE "${CMAKE_SOURCE_DIR}/Commands")
   ```
   placed right after the `NowPlayingLineTest` lines. Cases:
   - a playing song with loop queue and three short waiting songs: the
     exact text, as `/queue` gives it today;
   - nothing playing, two waiting: no "Now playing:" line;
   - exactly 15 short songs: all listed, no last line;
   - 20 short songs: 15 listed, `...and 5 more`;
   - 20 songs with labels of 149 bytes (the resolved worst case) and a
     playing song: the text is at most 2000 bytes, fewer than 15 are
     listed, the listed lines are whole, and listed plus N is 20;
   - 15 songs with labels of 252 bytes (long unresolved links): at most
     2000 bytes, listed plus N is 15;
   - the edge: labels sized so that the text is exactly 2000 bytes with the
     last song listed, and one byte more leaves that song out;
   - one waiting song whose label alone is longer than the limit: nothing
     listed, `...and 1 more`.

4. `README.md`: the `/queue` row says that it lists the first songs, fewer
   when their titles are long; the test sentence lists the new test.

## Checked, not changed

Report the worst case of each in the final report, from the code:
`/nowplaying`, the `/playlist` summary, the "Playing:" announcement, the
"Queued at position N:" edit and the `/remove` answer each carry one label
(or one playlist title and one link) and stay far below 2000.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class or namespace header, each bullet naming the function that enforces
it. Code that reads like its surroundings. Commit messages = short header +
`*` bullets, one line each, ending with the coding model's co-author trailer.
Nothing pushed. Nothing added or changed under `docs/`. `token.json` is never
read, printed or touched. The bot is never started, stopped or killed. DPP
untouched. Zero warnings. History is linear: no merge commits. YAGNI: no
paging, no option for the number of songs, no embeds.

One commit.

## Checks

- Build everything (bot and tests) in the agents' own build directory, zero
  warnings; `ctest`: all tests pass, the new one included.
- Owner's runtime pass: `/queue` with a few songs looks as before. Queue a
  playlist of songs with long titles, `/queue`: an answer comes, with fewer
  than 15 songs and a last line that counts the rest.
