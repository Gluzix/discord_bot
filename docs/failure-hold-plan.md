# Hold the song when failures come back to back

Branch `failure-hold` off master. Two commits (the class, then the wiring).

## The bug, from the log of 2026-09-19

The owner's internet dropped for about 100 seconds at 21:46:35, a few seconds
after a song had ended normally. Nothing was streaming, so the voice-loss
recovery never came into it. The next song failed to open, the worker dropped
it and popped the next one, which failed too, and so on: 80 songs between
21:46:40 and 21:47:41, one per second (20 at `open`, 60 in yt-dlp). A failed
song is not requeued even in queue-loop mode, so the looped playlist was gone
in 61 seconds. Each failure also posted its error message (80 POSTs, then a
rate limit), and five minutes later the idle timer left the voice channel.

The worker cannot tell "this song is broken" from "the network is down". One
failure is a broken song. Failures back to back are something bigger, and then
dropping songs is the wrong answer.

## The behaviour

- The first failure of a streak is handled exactly as today: the song is
  dropped and its error message goes out.
- From the second consecutive failure on, the song is kept: it goes back to
  the front of the queue, the worker waits 30 seconds and tries it again, up
  to 5 retries per song. Retries are quiet (no error message per attempt).
- When a streak first turns into a hold, the worker posts ONE notice in the
  song's text channel. Not again for later songs of the same streak.
- A song that still fails after its 5 retries is dropped like today (no new
  message, one log line). The streak goes on, so the next failing song is held
  at once.
- Anything but a failure ends the streak: a song that played, a skipped song,
  a lost voice session. So does `/stop`, and so does the queue running empty
  (failures far apart in time must not add up).
- `/skip` and `/stop` work during the wait. `/skip` treats the held song as
  the current one and lifts the wait; it does not end the streak.
- While a song is held the queue is not empty, so the idle timer leaves the
  bot in the channel. That is wanted.

## The change

### 1. `Playback/FailureStreak.h/.cpp` (commit 1)

Pure logic in the spirit of `VoiceDrainWatchdog`: no threads, no dpp, no Qt.
Add both files to the source list in `CMakeLists.txt` next to the watchdog.

```cpp
// Tells a broken song from a broken network: one failure drops the song,
// failures back to back keep it for a retry.
class FailureStreak
{
public:
    struct Decision
    {
        bool retry{false};         // keep the song and try it again
        bool firstOfStreak{false}; // this streak just turned into a hold: say so once
    };

    FailureStreak(int holdFromFailure_, int maxRetriesPerSong_);

    // One failed song; retriesSoFar is how often this very song was retried.
    Decision recordFailure(int retriesSoFar);

    // A song played, was skipped, or the queue ran empty.
    void reset();

    int length() const; // for the log

private: ...
};
```

`recordFailure` counts the failure, then: `retry` is true when the streak has
reached `holdFromFailure` and `retriesSoFar < maxRetriesPerSong`;
`firstOfStreak` is true for the first `retry == true` decision since the last
`reset()`, and only that one.

### 2. `Playback/Song.h`

`int failedAttempts{0}; // retries of a held song so far; they stay quiet`

### 3. `Playback/PlaybackController` (commit 2)

New members, both guarded by `stateMutex`:
`FailureStreak failureStreak{2, 5};` and
`std::chrono::steady_clock::time_point retryNotBefore{};` (the epoch default
means "no wait"). The 30 seconds live as a named constant next to them or in
the worker, e.g. `RETRY_AFTER`.

**The worker's wait** at the top of the loop becomes a loop that also honours
`retryNotBefore`. Sketch (same lock as today):

```cpp
auto songReady = [this] {
    return !songQueue.empty() && currentVoiceClient != nullptr
        && !songQueue.front().resolveInFlight;
};
while (running) {
    if (!songReady()) {
        stateCv.wait(lock);
    } else if (std::chrono::steady_clock::now() < retryNotBefore) {
        stateCv.wait_until(lock, retryNotBefore);
    } else {
        break;
    }
}
if (!running) break;
```

Every wake re-checks `running`, so shutdown still gets out at once (the
destructor already notifies).

**After the song**, in the existing critical section:

```cpp
const bool failed = outcome == SongPlayer::Outcome::Failed && !skipRequested;
if (!failed) failureStreak.reset();

if (outcome == VoiceLost) { ...unchanged... }
else if (failed && sessionAlive) {
    const FailureStreak::Decision decision = failureStreak.recordFailure(song.failedAttempts);
    if (decision.retry) {
        Song retry = makeReplay(song, false);
        retry.failedAttempts = song.failedAttempts + 1;
        retry.directUrl.clear();      // the url may be what failed
        retry.resolvedAtSeconds = 0;
        songQueue.push_front(std::move(retry));
        retryNotBefore = std::chrono::steady_clock::now() + RETRY_AFTER;
        announceHold = decision.firstOfStreak;
    }
}
else if (...loop branches unchanged...)
...
if (songQueue.empty()) { idleSinceSeconds = ...; failureStreak.reset(); }
```

`failed` must exclude `skipRequested`: a skip can land right after the
decoder decided to fail, and a skipped song is never held.

**The notice** goes out after the lock is released, next to
`reportVoiceLost` (never call dpp under `stateMutex`): a new message with
`messages::retryingAfterFailures`, `channel_id` from
`song.event->command.channel_id`, sent with
`song.event->owner->message_create(...)`, the way the decoder does it. The
worker's local `song` still owns its event there; the retry got a copy.

**Log lines** (`qWarning`, outside or inside the lock, no dpp involved): one
when a song is held, naming the streak length, the retry number out of the
maximum, and the song (`title`, or `target` while the title is unknown); one
when a song is given up after its retries. These are the first lines in the
log that name a song, keep them readable.

**`skip()`** during a hold. Today it returns 0 when no song is in progress.
New first case, under the same lock: no song in progress, queue not empty,
and `now < retryNotBefore` means a song is held at the front, and it is "the
current song". Take `min(max(count, 1), songQueue.size())` songs off the front
(queue-loop mode rotates them to the back as today; set the held song's
`failedAttempts` back to 0 first so its next turn starts clean), set
`retryNotBefore = {}`, notify `stateCv` after unlocking, return how many went.
Do not touch `skipRequested` and do not call `songPlayer->stop()`: there is no
song in the player. The streak is NOT reset here.

**`stop()`**: also `retryNotBefore = {};` and `failureStreak.reset();` in its
critical section.

### 4. `Playback/SongPlayer.cpp`, `decode()`

`fail()` still sets `currentSongFailed`, but only posts the message when
`song.failedAttempts == 0`. A successful retry announces "Playing:" as usual
(`makeReplay` sets `wasQueued`, so it is a fresh message).

### 5. `Messages.h`

Under "playback errors":
`retryingAfterFailures = "Songs keep failing - I'll retry this one for a few minutes before moving on."`

## What must not change

- The first failure of a streak: same drop, same message as today.
- `Outcome::VoiceLost` handling, the loop-mode branches, `skipRequested`
  semantics for a song in progress, the idle clock.
- No dpp call while `stateMutex` is held.
- Every wait of the worker ends when `running` turns false.
- `SongPlayer`'s threading is not touched; only `fail()` changes.

## Checks

1. Harness in the scratchpad (never in the repo), plain C++17 against the real
   `Playback/FailureStreak.cpp`, modelled on `harness/watchdog_test.cpp` there:
   one failure drops; the second in a row retries with `firstOfStreak`; the
   third retries without it; `retriesSoFar == max` drops while the streak goes
   on, and the next song's failure retries at once without `firstOfStreak`;
   `reset()` makes the next failure a plain drop again, and a new streak
   announces again; `length()` counts.
2. Build after each commit, zero warnings.
3. Owner's runtime pass: queue two dead links back to back and a good song
   behind them (`/play https://www.youtube.com/watch?v=aaaaaaaaaaa` twice, then
   a real one). Expected: first dead link gives its error and is dropped; the
   second gives its error plus the one notice, is retried quietly every 30 s
   (log lines), then given up after 5 retries and the good song plays. Again,
   with `/skip` during the wait: the good song starts at once. `/stop` during
   the wait, then a new `/play`: starts at once, and a failing song is a plain
   drop again.

## Rules

Comments only where really necessary and really short (the why, never the
what); commit message = short header + `*` bullets, one line each, ending with
the coding model's co-author trailer; nothing pushed; `docs/` stays out of
git; DPP, FFmpeg and yt-dlp untouched; don't run the bot, don't touch
`token.json`.
