# Seek and rewind plan

Goal: `/forward`, a new `/rewind` and a new `/seek` that land anywhere in the
current song within about half a second, and report the resulting position.
Today `/forward` drops PCM from the 60-second queue and, past that, makes the
decoder decode and discard up to the mark; going backwards is impossible.

## What already helps

`PcmResampler`'s `ChunkedSource` implements FFmpeg's byte-seek callback, so
`av_seek_frame` works over HTTP already: a seek is one new range request.

## Layering (unchanged)

`PlaybackController` (queue, policy) -> `SongPlayer` (threads, PCM queue, knows
dpp) -> `PcmResampler` (pure FFmpeg, knows no threads or Discord). Commands
only talk to the controller.

## 1. `PcmResampler`: seek requests and duration

```cpp
// Asks run() to jump to `seconds`; callable from any thread, and also while
// run() is not running (the next run() starts there). The ticket comes back
// in onSeeked so the caller can tell its latest request from an older one.
void requestSeek(double seconds, uint64_t ticket);

// 0 when the container doesn't say.
double durationSeconds() const;

using SeekDone = std::function<void(uint64_t ticket, bool ok)>;
void run(const PacketSink &sink, const std::function<bool()> &keepGoing, const SeekDone &onSeeked);
```

- The pending request is a small struct `{double seconds; uint64_t ticket; bool set;}`
  behind its own `std::mutex` (not an atomic pair). A newer request simply
  overwrites an unconsumed older one.
- `run()` consumes a pending request before its first read and at the top of
  every loop iteration:
  `av_seek_frame(format, audioStream, ts, AVSEEK_FLAG_BACKWARD)` with `ts` in
  the stream's time_base; then `avcodec_flush_buffers(codecContext)`, reset the
  resampler's delay line (`swr_init(swr)` again is enough), clear `staging`,
  remember `skipUntilSeconds = seconds`, and call `onSeeked(ticket, ok)`.
  On failure leave everything as it was (no flush, no skip) and report `ok=false`.
- Precision: a container seek lands on the cue at or before the target, which
  can be seconds early. After a seek, drop every decoded frame that ends at or
  before `skipUntilSeconds` (`frame->best_effort_timestamp` in the stream's
  time_base plus `nb_samples / sample_rate`); frames with no timestamp are
  kept. Opus frames are 20 ms, so no trimming inside a frame.
- `run()` must be restartable after it returned at end of stream: a rewind
  requested after EOF seeks, flushes the codec (which also clears its EOF
  state) and decodes again. Keep the EOF epilogue (resampler drain, short last
  packet) as it is.
- The three timing totals become members, accumulated across runs and logged
  once in the destructor, so a restarted run doesn't log a second, partial set.
- One `qDebug` line per seek: target, ticket, ok, and how long the seek took.

## 2. `SongPlayer`: position, seek, a decoder that stays for the whole song

Public API: `forward(int)` goes away.

```cpp
struct Position { double seconds{0}; double durationSeconds{0}; }; // duration 0 = unknown

// Relative and absolute jumps in the current song. nullopt when there is
// nothing to seek in right now: no song, still resolving/opening, or the
// song is already ending.
std::optional<Position> seekBy(int deltaSeconds);
std::optional<Position> seekTo(int seconds);
```

New state, all guarded by `queueMutex`:

| Member | Meaning |
|---|---|
| `uint64_t playedBytes` | song offset of the next packet the sender will pop |
| `uint64_t seekTicket` | incremented per real seek; the latest one wins |
| `bool seekInFlight` | the decoder has not applied the latest seek yet: its packets are stale |
| `uint64_t bytesBeforeSeek`, `size_t droppedForSeek` | to restore the position if the seek fails |
| `bool songEnding` | the sender took the natural-end exit; too late to seek |
| `bool flushClient` | asks the sender to `stop_audio()`; it is the only thread allowed on the client |
| `PcmResampler *activeResampler` | set by `decode()` after `open()`, cleared before the resampler dies |
| `double durationSeconds` | copied from the resampler after `open()` |

`bytesToDiscard` and the decode-and-discard path are deleted.

Seek algorithm (both entry points funnel into one private function, under
`queueMutex`):

1. `if (!isPlaying || songEnding || activeResampler == nullptr) return nullopt;`
2. `current = playedBytes / BYTES_PER_SECOND`; relative target = current + delta;
   clamp to `>= 0`, and to `durationSeconds` when it is known. A target at the
   end simply ends the song, as forwarding past the end does today.
3. Fast path, no decoder involved: a forward the queue fully covers
   (`seekInFlight` false, `0 < delta*BPS <= queuedBytes`): pop whole packets
   until that many bytes are dropped, add them to `playedBytes`,
   `flushClient = true`, notify, return the position.
4. Real seek: remember `bytesBeforeSeek = playedBytes` and
   `droppedForSeek = queuedBytes`; clear the queue; `seekInFlight = true`;
   `decodingFinished = false`; `flushClient = true`; `++seekTicket`;
   `activeResampler->requestSeek(target, seekTicket)`; set
   `playedBytes = target * BYTES_PER_SECOND` right away (optimistic, so a second
   `/forward` issued before the first lands builds on the first);
   `queueCv.notify_all()`; return the position.

Decoder side:

- The sink's wait predicate becomes
  `queuedBytes < MAX_QUEUED_BYTES || seekInFlight || !isPlaying`, and it drops
  the packet when `!isPlaying || seekInFlight` (a packet from before the seek).
- `onSeeked(ticket, ok)` (decoder thread, under `queueMutex`): ignore it unless
  `ticket == seekTicket`; then `seekInFlight = false`, and on `!ok` restore
  `playedBytes = bytesBeforeSeek + droppedForSeek` (the dropped queue was in
  effect a forward).
- The decoder must outlive end-of-stream, otherwise the last minute of every
  song (the decoder is 60 s ahead) cannot be rewound. After `open()`:
  ```cpp
  { lock; activeResampler = &resampler; durationSeconds = resampler.durationSeconds(); }
  for (;;) {
      resampler.run(sink, keepGoing, onSeeked);
      std::unique_lock<std::mutex> lock(queueMutex);
      decodingFinished = true;          // the sender may end the song once the queue drains
      queueCv.notify_all();
      queueCv.wait(lock, [this] { return seekInFlight || !isPlaying; });
      if (!isPlaying) break;
      decodingFinished = false;         // a rewind after EOF: decode again
  }
  { lock; activeResampler = nullptr; } // before the resampler goes out of scope
  ```
  The existing FinishGuard stays for the early-return paths.

Sender side:

- At pop: `playedBytes += packet.size()`.
- Natural end, still under the lock: `if (audioQueue.empty() && decodingFinished) { songEnding = true; break; }`
- The final `isPlaying = false` at the end of `streamAudio` MUST become
  `stop()`. The decoder now waits on `queueCv` until the song ends, and a plain
  store with no notify would leave it asleep and hang `play()`'s join on every
  song. (Same defect class as the voice-lost exits fixed earlier.)
- `flushClient`: checked at the top of the outer loop and inside the pacing
  loop. When set (read and clear it under `queueMutex`, act outside it):
  check `voiceClient->terminating` first, then `voiceClient->stop_audio()`, and
  throw away the packet currently held, since it is from before the jump; then
  restart the outer loop. This is what makes a seek audible at once instead of
  after dpp's one-second tail, and it also covers a seek during `/pause`, where
  the sender sits in the pacing loop holding a stale packet.
- `play()` resets all the new state at the start of a song.

## 3. `PlaybackController`

`int forward(int)` is replaced by:

```cpp
struct SeekResult { bool playing{false}; double positionSeconds{0}; double durationSeconds{0}; };
SeekResult seekBy(int deltaSeconds);
SeekResult seekTo(int positionSeconds);
```

Same gate as today (`clientIfSongInProgress()`), then the player call;
`playing=false` when either says no.

## 4. Commands and text

- `Helpers/TimeText.h/.cpp`, namespace `timetext`:
  `std::optional<int> parsePosition(const std::string &)` accepting `90`,
  `1:30`, `1:02:03` (minutes and seconds fields below 60 when a larger unit is
  present, no negatives, no empty fields) and
  `std::string formatPosition(double seconds)` giving `m:ss` or `h:mm:ss`.
- `/forward [seconds=10, 1..600]`: `seekBy(+n)`, reply `Forwarded to 2:35 / 4:10`.
- New `/rewind [seconds=10, 1..600]`: `seekBy(-n)`, reply `Rewound to 0:45 / 4:10`.
- New `/seek position` (string, required): `seekTo`, reply `Jumped to 1:30 / 4:10`;
  an unparsable position gets its own message.
- The ` / total` part is omitted when the duration is unknown.
- `playing=false` replies `nothingPlaying` when no song is in progress; when a
  song is in progress but can't be seeked yet, a new message saying the song is
  still loading. The controller result needs to tell the two apart (an enum or
  a second bool - the coder's choice, keep it small).
- `Messages.h`: add the new strings, delete `forwardedPrefix`,
  `forwardedSuffix` and `nothingBuffered` once nothing references them.
- Register both new commands in `CommandHandler::setupCommands`, add the new
  files to `CMakeLists.txt`, add the two commands to the README's command list.
- Both new commands call `userMayControl(event)` first, like `/forward`.

## Commits (branch `seek-rewind` off master)

1. `PcmResampler`: seek requests, duration, restartable run, timing totals in
   the destructor.
2. `SongPlayer` + `PlaybackController` seek with position tracking, the
   decoder kept alive, the sender's flush and `stop()` at the end; `TimeText`
   and the new messages; `/forward` switched to the new API (so this commit
   builds and behaves).
3. `/rewind` and `/seek`, registration, CMake, README.

House rules: comments only where really necessary and really short (the why,
never the what); commit messages as a short header plus `*` bullets
(added/deleted/improved/fixed), one line each, ending with the co-author
trailer naming the coding model; nothing pushed; DPP is not modified.

## Verification

1. Build after every commit, zero warnings. If the owner's bot is running,
   `LNK1168` at link is expected; then verify the link out of place.
2. Resampler harness (scratchpad, not the repo), linking the real
   `Playback/PcmResampler.cpp` against FFmpeg and Qt like the earlier
   `decode_test`: resolve a long upload first with
   `yt-dlp -f bestaudio --print urls "https://www.youtube.com/watch?v=olYBjqIjDUQ"`
   (a 39-minute album), then check:
   a. seek accuracy: request a seek to `duration - 30` before `run()`, count the
      PCM bytes until EOF, expect 30 s worth within one second;
   b. latency: from `requestSeek` during a running decode to the first
      post-seek packet, expect well under a second, and `onSeeked` fires with
      the right ticket and `ok=true`;
   c. restart after EOF: let `run()` return at EOF, request a seek to 10 s,
      call `run()` again, expect packets to flow;
   d. a newer request overwrites an unconsumed older one (only the newer
      ticket comes back).
3. `TimeText` checks: `90`->90, `1:30`->90, `1:02:03`->3723, `abc`, `1:75`,
   `-5`, `1::3` and empty all rejected; `95`->`1:35`, `3723`->`1:02:03`,
   `5`->`0:05`.
4. Runtime checks are the owner's: `/forward` 10, 60 and 600; `/rewind 30`;
   a rewind inside the last 30 s of a song; `/seek 1:30`; five forwards spammed
   quickly; a seek while paused; skip right after a seek; both loop modes;
   a song ending normally (the decoder join must not hang); Ctrl+C mid-song.
