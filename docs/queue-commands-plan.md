# /replay, /shuffle and /remove

Branch `queue-commands` off master. Three new slash commands, no buttons.
All three are playback control, so all three ask `userMayControl()`.

YAGNI: no undo, no `/remove` by title or range, no shuffle mode that stays
on, no button for any of them.

## Behaviour

- `/replay` starts the current song over. It is `/seek 0` with its own
  answer: "Starting the song over!". Nothing playing and a song that is
  still loading answer as `/seek` does (`messages::nothingPlaying`,
  `messages::songStillLoading`), and like `/seek` it acts on whatever is
  current when it arrives and leaves a paused song paused.
- `/shuffle` puts the waiting songs into a random order and answers
  "Shuffled the queue!". The song that plays is not touched. With fewer than
  two songs to shuffle it answers "There's nothing to shuffle!" and changes
  nothing.
- `/remove position:<n>` takes one waiting song out. `n` counts the waiting
  songs from 1, exactly as `/queue` numbers them. The answer names the song,
  because the queue can move between a look at `/queue` and the command:
  `Removed <label> from the queue.`, with mentions switched off (the title
  is untrusted). A position the queue does not have answers "There's no
  song at that position - check /queue!".
- The pinned front. Three kinds of song at the front of the queue are the
  current song waiting for its turn, not a waiting song:
  - one that is held for a retry (`failedAttempts > 0`);
  - the replay of a looping song (`isLoopReplay`);
  - one that was asked for while nothing played and has not started yet
    (`!wasQueued`: still resolving, or the voice connection is not there).
  `/shuffle` leaves such a song where it is. `/remove` on it answers "That
  one is being retried - use /skip for it!" for a held song and "That one
  is about to play - use /skip once it does!" for the other two. Moved away
  from the front, a held song would break the hold (the controller finds it
  at the front), a loop replay would play later without an announcement,
  and the third kind would announce itself by editing its old `/play`
  reply, far up in the channel or too old to be edited at all.
- A song put back after a lost voice session is an ordinary waiting song:
  it announces itself afresh wherever it plays.

## Files

1. `Playback/QueueEdit.h/.cpp`: pure, no lock, no dpp call.
   ```cpp
   // What /shuffle and /remove do to the waiting songs.
   // =======================================================
   // Rules:
   // - A front song that is held for a retry, that replays a looping song,
   //   or that was asked for while nothing played and has not started yet,
   //   is the current song waiting for its turn: pinnedFront() counts it,
   //   so shuffle() leaves it in front and remove() will not take it. Moved,
   //   a held song breaks the hold, which looks for it at the front; a loop
   //   replay plays later without its announcement; and the third kind
   //   announces by editing its own /play reply, which by then is far up
   //   the channel or too old to edit.
   // =======================================================
   namespace queueedit {

   // 1 when the front song must stay where it is, else 0.
   size_t pinnedFront(const std::deque<Song> &queue);

   // Shuffles the songs behind the pinned front and returns how many
   // those are; fewer than two are left as they are.
   size_t shuffle(std::deque<Song> &queue, std::mt19937 &rng);

   enum class RemoveOutcome {
       Removed,
       NoSuchPosition,
       Held,           // the pinned front, held for a retry: /skip takes it
       UpNext,         // the pinned front, about to play
   };

   struct Removal
   {
       RemoveOutcome outcome{RemoveOutcome::NoSuchPosition};
       std::optional<Song> song; // the song taken out, when Removed
   };

   // position counts the waiting songs from 1, as /queue numbers them.
   Removal remove(std::deque<Song> &queue, size_t position);

   }
   ```
   - `QueueEdit.cpp` includes `<dpp/dpp.h>`: moving and destroying a `Song`
     needs the complete `dpp::slashcommand_t` behind its `unique_ptr`.
     `QueueEdit.h` includes no dpp or Qt header: `PlaybackController.h`
     includes it, and `NowPlayingLineTest` compiles that header without
     dpp's include path.
   - `remove()`: position 0 or past the end is `NoSuchPosition`; a position
     inside the pinned front is `Held` when that song has
     `failedAttempts > 0`, else `UpNext`; otherwise the song is moved out
     and erased.

2. `Playback/PlaybackController.h/.cpp`:
   ```cpp
   // Shuffles the waiting songs and returns how many took part; fewer than
   // two means nothing changed.
   size_t shuffle();

   struct RemoveResult
   {
       queueedit::RemoveOutcome outcome{queueedit::RemoveOutcome::NoSuchPosition};
       std::string label; // rendered markdown of the song taken out
   };

   // Takes the waiting song at position (from 1, as /queue numbers them) out.
   RemoveResult remove(size_t position);
   ```
   - Both take `stateMutex`, call the `queueedit` function (qualified:
     inside the members the bare names find the members themselves),
     release the lock and then `stateCv.notify_all()`: the worker's wait
     depends on the front, the resolver's lookahead on the first five.
   - A member `std::mt19937 shuffleRng{std::random_device{}()};`, used under
     `stateMutex` only.
   - `remove()` renders the label with `labels::render()`. The removed
     `Song` dies where it is removed, as in `skip()` and `stop()`.
   - When `remove()` leaves the queue empty while no song is in progress,
     the idle clock starts and the failure streak ends, as the rules block
     demands of `skip()` and `stop()`; that bullet names `remove()` too.
   - No dpp call under `stateMutex`.

3. `Commands/ReplayCommand.h/.cpp`, `Commands/ShuffleCommand.h/.cpp`,
   `Commands/RemoveCommand.h/.cpp`, modelled on `SeekCommand` (no button, so
   they answer with `event.reply(...)`), registered in
   `CommandHandler::setupCommands()`:
   - `ReplayCommand`: name `replay`, description "start the current song
     over"; `playback->seekTo(0)`. No `definition()` override, like
     `StopCommand`.
   - `ShuffleCommand`: name `shuffle`, description "shuffle the waiting
     songs"; `playback->shuffle()`. No `definition()` override.
   - `RemoveCommand`: name `remove`, description "take a song out of the
     queue"; one required option `position`, `dpp::co_integer`, minimum 1,
     "its number in /queue". The value is read as `int64_t` and cast to
     `size_t` without clamping (a clamp would turn a bad value into a real
     position). The answer for `Removed` is a `dpp::message` with
     `set_allowed_mentions()`, as `QueueCommand` builds its answer. A
     `switch` without `default` over the outcome, inside the void
     `execute()`; the case that declares the message gets braces.

4. `Messages.h`, in a new block `// /replay, /shuffle, /remove`:
   `replaying = "Starting the song over!"`,
   `shuffled = "Shuffled the queue!"`,
   `nothingToShuffle = "There's nothing to shuffle!"`,
   `removedPrefix = "Removed "`, `removedSuffix = " from the queue."`,
   `noSuchPosition = "There's no song at that position - check /queue!"`,
   `removeHeld = "That one is being retried - use /skip for it!"`,
   `removeUpNext = "That one is about to play - use /skip once it does!"`.

5. `tests/QueueEditTest.cpp`:
   ```cmake
   add_bot_test(QueueEditTest QueueEditTest.cpp Check.h ../Playback/QueueEdit.cpp)
   target_link_libraries(QueueEditTest PRIVATE dpp)
   set_tests_properties(QueueEditTest PROPERTIES ENVIRONMENT_MODIFICATION "${DPP_PATH}")
   ```
   The test includes `<dpp/dpp.h>` as well: it creates and destroys Songs.
   Songs are told apart by their ids, and a plain waiting song has
   `wasQueued = true` (the struct's default is false, which means pinned).
   The shuffles use `std::mt19937` with the fixed seed 42.
   - `pinnedFront`: an empty queue; a plain front; a held front; a
     loop-replay front; a front with `wasQueued == false`; a held song that
     is not at the front (0).
   - `shuffle`: ten songs come back as the same ten ids, each once, in
     another order, and the count is 10; the same seed gives the same order
     twice; a pinned front stays first and the count is one less; one song
     and no song are left alone and count 1 and 0; two songs behind a
     pinned front give count 2, the front still first and the same two ids
     behind it (their order is not asserted).
   - `remove`: position 0 and a position past the end are `NoSuchPosition`
     and change nothing; a middle position gives `Removed` with that song's
     id and the others keep their order; position 1 of a plain queue is
     `Removed`; position 1 with a held front is `Held`, with a loop-replay
     front and with a not-yet-started front `UpNext`, each changing
     nothing; position 2 with a pinned front is `Removed`.

6. `CMakeLists.txt` (the new files next to their neighbours) and
   `README.md`: three rows in the command table, after `/skip`
   (`/replay`), and after `/queue` (`/shuffle`, `/remove position:<n>`);
   "How it works" names `QueueEdit` where `FailureStreak` is named; the test
   sentence lists the new test.

## Rules

Comments only where really necessary and really short (the why, never the
what); a fact the code cannot show goes into the rules block at the top of
the class or namespace header, each bullet naming the function that enforces
it. Code that reads like its surroundings. Commit messages = short header +
`*` bullets, one line each, ending with the coding model's co-author trailer.
Nothing pushed. Nothing added or changed under `docs/`. `token.json` is never
read, printed or touched. The bot is never started, stopped or killed. DPP
untouched. Zero warnings. History is linear: no merge commits.

Commits, each building and passing on its own: (1) `QueueEdit` with its
test; (2) `/shuffle`; (3) `/remove`; (4) `/replay`. Each command's commit
carries its messages and its README row.

## Checks

- Build everything after each commit (bot and tests), zero warnings, in the
  agents' own build directory - never in the owner's Qt Creator one, where
  his bot may be running.
- `ctest`: all tests pass, the new one included.
- Owner's runtime pass:
  1. `/replay` mid-song: it starts over and the answer shows. `/replay`
     with nothing playing: "Nothing is playing right now!".
  2. Queue five songs, `/queue`, `/shuffle`, `/queue`: another order, the
     same five, the playing song untouched (about 1 time in 24 four songs
     come back in the order they had; shuffle again). `/shuffle` with one
     waiting song: "There's nothing to shuffle!".
  3. `/remove 2`: the answer names the second song and `/queue` no longer
     has it. `/remove 99`: no such position. `/remove 0` cannot be typed
     (Discord enforces the minimum).
  4. After a shuffle the next songs still start without a pause (the
     resolver looked ahead again).
  5. From outside the bot's voice channel, with someone else in it, all
     three are refused.
