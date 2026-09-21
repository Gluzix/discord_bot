# SongPlayer split plan

Goal: `SongPlayer` becomes a commander. The decoder and the sender become
their own classes, and the state they share gets a class of its own with a
real API. Pure refactor: no behaviour change, same log lines.

Precondition: the seek work (`seek-rewind`) is merged and has had a soak, and
the owner's `ChunkedSource` split is in. This plan is written against the
player as it is on `seek-rewind`; re-read the code before starting.

## Why the first cut is the shared state, not the threads

The decoder and the sender are not coupled by sharing a class. They are
coupled by about a dozen variables under one mutex: the packet queue,
`queuedBytes`, `playedBytes`, `seekTicket`, `seekInFlight`, `bytesBeforeSeek`,
`droppedForSeek`, `songEnding`, `flushClient`, `decodingFinished`,
`activeResampler`, `durationSeconds`, plus `isPlaying` and `queueCv`. Moving
the two thread bodies into two classes first would leave both reaching into
that state through references, and the coupling would merely become invisible
across three files. So the protocol is extracted first; the workers then fall
out almost mechanically. All three concurrency defects caught in review so far
lived in this protocol, and today nothing can test it without Discord.

## Target shape

```
PlaybackController            queue, loop policy, voice session
  SongPlayer                  commander: one song's lifecycle, outcome, seek API
    PcmBuffer                 the shared protocol - no dpp, no FFmpeg, no Qt
    DecoderWorker  (thread)   resolve, announce, PcmResampler -> buffer's producer side
    SenderWorker   (thread)   pacing, watchdog, flush, dpp    <- buffer's consumer side
```

Files: `Playback/PcmBuffer.h/.cpp`, `Playback/DecoderWorker.h/.cpp`,
`Playback/SenderWorker.h/.cpp`. `SongPlayer.h/.cpp` stay and shrink.

## 1. `PcmBuffer`: the protocol behind methods

Owns privately: one `std::mutex`, one `std::condition_variable`, every variable
listed above, and the live flag that is `isPlaying` today (an atomic, because
two pollers read it without the lock: the resampler's `keepGoing` and the
yt-dlp cancel check).

Control side (the commander and, through it, the commands):

```cpp
void reset();                 // per song; must NOT clear the live flag - arm() comes before play()
void arm();                   // live = true
void stop();                  // live = false under the mutex, then notify_all
bool running() const;         // lock-free read

struct Position { double seconds{0}; double durationSeconds{0}; };
std::optional<Position> seek(double seconds, bool relative); // today's SongPlayer::seek, whole
bool cutShort();              // !(decodingFinished && queue empty): play()'s flush decision
```

Producer side (the decoder):

```cpp
enum class PushResult { Queued, Stale, Stopped };
PushResult push(std::vector<uint8_t> packet);   // blocks while full; Stale = a seek is in flight

void setDuration(double seconds);
void setSeekRequester(std::function<void(double seconds, uint64_t ticket)> requester);
void clearSeekRequester();                      // before the resampler dies
void seekApplied(uint64_t ticket, bool ok);     // today's onSeeked: only the newest ticket counts

void markFinished();                            // for the early-exit paths (today's FinishGuard)
bool finishedAndWaitForRewind();                // marks finished, notifies, blocks until a seek
                                                // (true: decode again) or a stop (false)
```

The seek requester replaces `activeResampler`: the buffer must not know
`PcmResampler`, so the decoder registers a callable that forwards to
`resampler.requestSeek`. It is invoked under the buffer's mutex, exactly as
`requestSeek` is today.

Consumer side (the sender):

```cpp
struct Next
{
    enum class Kind { Packet, Flush, Ended, Stopped } kind;
    std::vector<uint8_t> packet;                // only for Kind::Packet
};
Next next();                                    // blocks; Packet pops and counts playedBytes and
                                                // wakes the producer; Ended sets songEnding
bool takeFlushRequest();                        // inside the pacing loop
void pacingWait(std::chrono::milliseconds);     // wakes early on stop or on a flush request
```

## 2. `DecoderWorker`

`DecoderWorker(PcmBuffer &, Song &, std::function<void(std::string)> onLabelResolved)`.
Starts its thread in the constructor; `join()` is explicit and idempotent, the
destructor joins if nobody did. `bool failed() const`, valid after `join()`.

It takes today's `SongPlayer::decode` whole: the prefetch-freshness check, the
yt-dlp resolve with `!buffer.running()` as the cancel check, the write-back of
re-resolved fields into the song, `notifyUser` and `fail`, the label callback,
`PcmResampler::open`, the "Playing:" announcement, publishing duration and
seek requester, the run loop around `finishedAndWaitForRewind()`, clearing the
requester. Thread name `decoder`.

## 3. `SenderWorker`

`SenderWorker(PcmBuffer &, dpp::discord_voice_client *)`. Same thread
ownership as the decoder. `bool voiceLost() const`, valid after `join()`.

It takes today's `SongPlayer::streamAudio` whole: `MAX_BUFFERED_SECONDS`, the
`VoiceDrainWatchdog`, the `terminating` checks, the flush handling, the pacing
loop, `send_audio_raw`. Every exit ends in `buffer.stop()`. Thread name
`sender`.

## 4. `SongPlayer`, the commander

Members: a `PcmBuffer` and the label callback. `arm`, `stop`, `seekBy`,
`seekTo` forward to the buffer. `play()` becomes, in this order:

1. `buffer.reset()`
2. construct the decoder worker, then the sender worker
3. `sender.join()`
4. `if (sender.voiceLost()) { decoder.join(); return Outcome::VoiceLost; }` with
   no call into the client
5. `if (buffer.cutShort()) client->stop_audio();` then lift a pause
6. `decoder.join()`
7. `return decoder.failed() ? Outcome::Failed : Outcome::Finished;`

The public API of `SongPlayer` does not change, so `PlaybackController` and the
commands are untouched.

## Invariants that must survive (the acceptance criteria)

1. Every path that ends a song flips the live flag under the buffer's mutex
   and notifies all waiters. A plain store is a hung join; this has been found
   in review twice.
2. No dpp call while the buffer's mutex is held. The sender is the only thread
   that calls the voice client. `terminating` is checked before every client
   call. After `VoiceLost`, nothing calls the client, `play()` included.
3. Lock order: buffer mutex, then the resampler's pending-seek mutex, never
   the reverse. `seekApplied` is called with no resampler lock held.
4. The seek requester is cleared under the buffer's mutex before the
   resampler is destroyed, on every path that set it.
5. From a real seek request until `seekApplied` arrives with the newest
   ticket, every `push` answers `Stale`, including for the packet a producer
   blocked on a full buffer is holding.
6. `reset()` never disarms; `arm()` always precedes `play()`.
7. Join order in `play()`: sender, then the client flush, then decoder.
8. The voice-recovery behaviour (`VoiceDrainWatchdog`, `Outcome::VoiceLost`)
   and the seek behaviour are bit-for-bit what they are today.

## Steps (two branches, or at least two commits)

A. `PcmBuffer` and its harness. `SongPlayer` uses the buffer internally but
   keeps both thread bodies as member functions for now, so the diff reads as
   "state moved behind methods" and nothing else. Build, harness, runtime smoke.
B. Move the two thread bodies into `DecoderWorker` and `SenderWorker`. Mostly
   mechanical once A is in.

House rules as always: comments only where really necessary and really short;
commit format with `*` bullets and the coding model's co-author trailer;
nothing pushed; DPP and FFmpeg untouched.

## Verification

1. A `PcmBuffer` harness in the scratchpad, plain C++ with fake producer and
   consumer threads, no dpp, FFmpeg or Qt. Cases: a push blocks at the cap and
   resumes after a pop; `stop()` wakes a blocked producer, a blocked consumer
   and a pacing wait; after a real seek every push is `Stale` until
   `seekApplied` with the newest ticket, and an older ticket changes nothing;
   the fast-path forward drops whole packets and moves the position; a failed
   seek restores the position; `Ended` only when finished and empty;
   a seek after `finishedAndWaitForRewind()` revives the producer; a seek
   after `Ended` is refused; a flush request wakes the pacing wait; position
   arithmetic across pops, fast-path drops and real seeks.
2. Build after each step, zero warnings.
3. The owner's runtime pass: the seek list (forward 10/60/600, rewind 30, a
   rewind in the last 30 s, `/seek 1:30`, five forwards spammed, a seek while
   paused, skip right after a seek), a song ending normally, both loop modes,
   the suspend test from voice recovery, Ctrl+C mid-song.
