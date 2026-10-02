# /playlist says what happened

Branch `playlist-truth` off origin/master (88bae82). The owner's tree is
on `announce-failure`, which he is testing, so the work happens in a
worktree of its own, with a build directory of its own. This plan and
`playlist-cap-findings.md` are untracked files of the owner's tree and
are not in the worktree; the brief names them by their full path.

The plan went through its check on 2026-09-29. The cap itself stays at
100.

## The problems

1. The summary of `/playlist` ends with "(it has N - I took the first
   ones)" whenever the playlist has more videos than were queued
   (`listing.totalCount > queued` in `Commands/PlaylistCommand.cpp`). Songs
   that were left out make `queued` smaller too. A public video whose
   title came back as "NA" is left out and counted, so a playlist of 50
   with one such video answers "Queued 49 songs from ... (it has 50 - I
   took the first ones)", although nothing was cut. Whether YouTube's
   count includes private and deleted videos was not measured; if it does,
   they bring the note up in the same way.
2. yt-dlp prints nothing until it has fetched the whole list. When the
   60 seconds of the runner pass first, the runner kills it, the listing
   is empty, and the bot answers "Couldn't find any playable videos in
   that playlist :(". The user cannot tell a slow YouTube from an empty
   playlist. When the kill falls into the short time in which yt-dlp
   prints, the bot gets a part of the list, without the title and the
   count, and queues it as if it were whole.

## The fix

1. The note appears when the playlist has more videos than the bot takes:
   `listing.totalCount > MAX_ENTRIES`. Nothing else in the summary
   changes. A playlist whose size yt-dlp does not name (a mix) gets no
   note, as today.
2. The runner says when it killed a run because the time was up, the
   resolver hands that on, and `/playlist` answers "Reading the playlist
   took too long - try again in a moment!". A listing that timed out is
   not trusted: nothing of it is queued, whatever had arrived.

YAGNI:
- No `--lazy-playlist`, no longer timeout, no retry.
- A yt-dlp that could not be started, or that ended with an error, still
  answers "Couldn't find any playable videos in that playlist :("; the
  log says what happened.
- `resolveMedia()` does not look at the new flag: a song whose lookup
  timed out fails as it does today.
- The runner keeps the lines that arrived before the kill, as today. It
  is `listPlaylist()` that drops them.
- The summary does not count the songs that were left out.
- The text of the summary stays in `PlaylistCommand::execute()`; no new
  unit for it.

## Files

Commit 1, the timeout:

1. `Runner/IProcessRunner.h`, in `Output`, after `cancelled`:
   ```cpp
   bool timedOut{false};           // killed because the timeout passed
   ```

2. `Runner/WindowsProcessRunner.cpp`, in `run()`: `result.timedOut = true`
   goes into the existing `if (!result.cancelled)` of the branch that
   kills the job, next to the "killed after" line. A cancelled run is not
   a timed out one, also when the deadline has passed in the same poll.
   Nothing else in `run()` changes; `lines` is filled as today.

3. `Resolver/IMediaResolver.h`, in `PlaylistListing`, after `totalCount`:
   ```cpp
   bool timedOut{false};               // ran out of time; entries is empty then
   ```
   The comment of `listPlaylist()` gets one more sentence: "A listing that
   timed out comes back empty, with timedOut set."

4. `Resolver/YtDlpResolver.cpp`, in `listPlaylist()`: the declaration
   `PlaylistListing listing;` moves up to right after the exit-code line,
   and under it:
   ```cpp
   if (run.timedOut) {
       listing.timedOut = true;
       return listing;
   }
   ```

5. `Messages.h`, in the `/playlist` block, after `playlistEmpty`:
   ```cpp
   inline constexpr const char* playlistTimedOut        = "Reading the playlist took too long - try again in a moment!";
   ```

6. `Commands/PlaylistCommand.cpp`, in `execute()`: the empty listing
   answers `messages::playlistTimedOut` when `listing.timedOut`, else
   `messages::playlistEmpty`.

7. `tests/YtDlpResolverTest.cpp`, two more blocks after "a playlist that
   ends mid-entry". Each builds its own runner, as the block "what a
   playlist asks for" does; the helper `listPlaylist(lines)` and its
   callers stay as they are. Both blocks use the same four lines:
   `"https://www.youtube.com/watch?v=a"`, `"Song A"`,
   `"PLAYLIST_TITLE=Mix"`, `"PLAYLIST_COUNT=250"`.
   - a playlist that timed out: `Output killed = ended(1, {the four lines});
     killed.timedOut = true;` The listing has `timedOut` set, no entries,
     an empty title and a count of 0.
   - a playlist that ended with an error: `ended(1, {the four lines})`.
     `timedOut` is not set, and the entry, the title and the count are
     read as usual.
   The two differ only in the flag: a resolver that took any exit code
   but 0 for a timeout fails the second, and one that set the flag and
   read on fails the first.

Commit 2, the note:

8. `Commands/PlaylistCommand.cpp`, in `execute()`: the note's condition
   becomes `listing.totalCount > MAX_ENTRIES`.

`README.md`: nothing. Its sentences about `/playlist` (the first 100) stay
true, the only failure message it quotes is a song's, and the tests
sentence names the yt-dlp resolver, to which the new cases belong.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class header, each bullet naming the function that enforces it. Code
that reads like its surroundings. C++17. Commit messages = short header +
`*` bullets, one line each, ending with the coding model's co-author
trailer. Nothing pushed. Nothing added or changed under `docs/`.
`token.json` is never read, printed, copied or touched. The bot is never
started, stopped or killed. DPP untouched. Zero warnings. History is
linear: no merge commits.

Two commits, in this order, each built, tested and committed before the
next is begun: both touch `Commands/PlaylistCommand.cpp`, and files are
staged whole, by name, never with `git add -A` or `git commit -a`.

After the review and its fix commits the orchestrator removes the
worktree, so that the branch is checked out nowhere and the owner can
take it into his tree.

## Checks

- Build everything (bot and tests) in the build directory of the
  worktree; zero warnings in the ninja output; `ctest`: all 12 tests
  pass, after each commit.
- A probe outside the repository runs the real `WindowsProcessRunner`
  with a timeout of 2 seconds, and looks after each run whether a
  `PING.EXE` is left:
  1. `ping -n 30 127.0.0.1`: `timedOut` is set, `cancelled` is not, and
     `run()` returns after about 2 seconds;
  2. the same with a cancel check that sleeps 2.1 seconds and then says
     yes, so that the cancel and the deadline hold in the same poll:
     `cancelled` is set, `timedOut` is not;
  3. `ping -n 2 127.0.0.1`: neither is set.
  A probe that compiles a Qt source needs `/Zc:__cplusplus` and
  `/permissive-`; the brief carries the command.
- Owner's runtime pass:
  1. `/playlist` with a playlist of more than 100 videos: the summary has
     the note, as before.
  2. `/playlist` with a short playlist that holds a private or deleted
     video: the summary has no note. If the build from master shows no
     note for it either, YouTube's count leaves such videos out, and this
     step shows nothing about the fix.
  3. The timeout cannot be provoked on purpose without changing the code;
     the probe and the test stand in for it.
