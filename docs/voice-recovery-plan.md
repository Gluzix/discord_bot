# Voice recovery plan (issue #10: bot can't reconnect to the voice channel)

Implementation plan for whoever codes it. Everything needed is in this file;
the DPP facts below were read from the DPP 10.1.6 sources, not guessed.

## What goes wrong today

1. Discord drops the voice websocket (it does that now and then; twice in one
   evening in the report). DPP retries once a second and sends a RESUME each
   time; Discord answers close code 1002 "malformed frame". DPP would fall back
   to a full reconnection after five failures inside a three-second window,
   but in the report its third attempt died silently and nothing followed.
   DPP 10.1.6 is the newest release. This part is DPP's bug and is NOT to be
   fixed in DPP (project rule: no library patches, no custom builds).
2. On every retry DPP's `cleanup()` deletes the UDP socket, the opus encoder
   and the courier thread, so its send buffer (`outbuf`) never drains again.
   `get_secs_remaining()` therefore stays above one second forever.
3. Our sender thread (`SongPlayer::streamAudio`, the pacing loop at
   `while (isPlaying && voiceClient->get_secs_remaining() > MAX_BUFFERED_SECONDS)`)
   waits on exactly that, forever. The song never ends, `songInProgress` stays
   true, the queue is stuck, and the pointer to the client dangles if DPP ever
   destroys it. Nothing observable on the client says "dead": `is_ready()`
   still returns true (the secret key is never cleared) and `terminating` is
   false. The only fact we can observe is "the buffer stopped draining".
4. The 3% CPU is DPP's socket engine polling the half-closed socket.

So the fix is on our side and has three parts: detect the dead client
(watchdog), put the song back and drop the client (controller), and get a
fresh connection through IDENTIFY, which works, rather than RESUME, which
does not (rejoiner). Plus one log fix that surfaced in the same report.

## Architecture

Keep the existing layering: `PlaybackController` (queue, policy, session)
-> `SongPlayer` (per-song threads, knows dpp voice) -> `PcmResampler` (pure
FFmpeg). Commands and DPP event wiring live in `CommandHandler`. New pieces:

| New | Where | Knows |
|---|---|---|
| `VoiceDrainWatchdog` | `Playback/VoiceDrainWatchdog.h/.cpp` | nothing but numbers and time |
| `VoiceRejoiner` | `VoiceRejoiner.h/.cpp` (top level, next to `VoiceConnector`) | dpp cluster/shard; not the controller |

The controller never sees the rejoiner; it reports "voice lost" through a
callback that `CommandHandler` wires to the rejoiner, the same way it wires
`on_voice_ready` today.

## 1. `VoiceDrainWatchdog` (pure logic, no dpp)

```cpp
// Says when a voice client's send buffer has stopped draining. Pure
// bookkeeping: the sender feeds it observations, it says when to give up.
class VoiceDrainWatchdog
{
public:
    using Clock = std::chrono::steady_clock;

    explicit VoiceDrainWatchdog(std::chrono::milliseconds deadAfter_);

    // One observation of the client's buffer. Returns true once the buffer
    // has not shrunk for deadAfter. A paused client is not a dead one.
    bool observe(float secsRemaining, bool paused, Clock::time_point now = Clock::now());

    // A packet went out: draining is proven, start over.
    void reset();

private:
    const std::chrono::milliseconds deadAfter;
    bool primed{false};
    float lastRemaining{0.0f};
    Clock::time_point stuckSince;
};
```

Semantics of `observe`:
- first call: `primed = true; stuckSince = now; lastRemaining = value;` return false
- `paused`: `stuckSince = now; lastRemaining = value;` return false
- `value < lastRemaining - 0.01f`: progress, `stuckSince = now`
- then `lastRemaining = value;` return `now - stuckSince >= deadAfter`

`reset()` puts it back to unprimed.

Threshold used by the player: `VOICE_DEAD_AFTER = 10 s`. Do not go lower, see
the DPP timer hazard below. A healthy client shrinks its buffer every 20 ms,
and the sender observes it every 100 ms, so ten seconds is far outside normal.

## 2. `SongPlayer` changes

- `bool play(...)` becomes `Outcome play(...)` with
  `enum class Outcome { Finished, Failed, VoiceLost };`
  (`Finished` == today's `true`, `Failed` == today's `false`).
- New member `bool voiceLost = false;` written by the sender thread, read by
  `play()` after the join (same pattern and comment as `currentSongFailed`).
  Reset at the top of `play()` with the other per-song state.
- In `streamAudio`, per song, one `VoiceDrainWatchdog watchdog(VOICE_DEAD_AFTER);`.
  In the pacing loop, each iteration before calling into the client:
  ```cpp
  // DPP sets terminating at least 100 ms before it destroys the client.
  if (voiceClient->terminating) { voiceLost = true; break; }
  if (watchdog.observe(voiceClient->get_secs_remaining(), voiceClient->is_paused())) { voiceLost = true; break; }
  ```
  (the `while` condition already calls `get_secs_remaining()`; restructure so
  it is called once per iteration.) Check `terminating` once more right before
  `send_audio_raw`, and `watchdog.reset()` after a successful send.
  A `break` out of the pacing loop must also leave the outer `while`; keep the
  existing structure where the outer loop ends when `isPlaying` is false, and
  set `isPlaying = false` on voice lost (the decoder stops through `keepGoing`).
- In `play()`, after `senderThread.join()`: if `voiceLost`, do NOT call
  `stop_audio()`, `is_paused()` or `pause_audio()`; join the decoder and return
  `Outcome::VoiceLost`. The client may already be gone.
- `terminating` is a public plain bool on `dpp::discord_voice_client`, written
  by DPP's thread. Reading it racily is fine: it is a hint with a 100 ms grace
  period that DPP's own threads rely on too.

## 3. `PlaybackController` changes

- `setVoiceLostHandler(std::function<void(uint64_t guildId, uint64_t channelId)>)`.
  Stored member; invoked from the worker thread, never while holding
  `stateMutex`.
- In `playbackWorker`, replace `bool ok` with the outcome:
  - `Finished`: today's loop-mode requeue logic (`ok == true`).
  - `Failed`: today's no-requeue logic (`ok == false`).
  - `VoiceLost`, under `stateMutex`:
    ```cpp
    bool sessionAlive = running && currentVoiceClient != nullptr;   // /stop meanwhile => nothing to resume
    bool sameClient = currentVoiceClient == voiceClient;             // DPP may already have handed us a new one
    if (sessionAlive) songQueue.push_front(makeReplay(song, false)); // announces "Playing:" again when it resumes
    if (sessionAlive && sameClient) { currentVoiceClient = nullptr; needRejoin = true; }
    skipRequested = false;
    ```
    then the common cleanup that already exists (songInProgress = false,
    label cleared, idle clock, notify). After the lock: if `needRejoin` and a
    handler is set, call it with `activeGuildId, activeChannelId`.
    Why `sameClient`: if DPP's own full reconnection already delivered a new
    client through `onVoiceReady` during the dead song, keep it and just let
    the worker continue; a rejoin would throw away a working connection.
- `onVoiceReady` stays as is: it sets the new client and notifies, and the
  worker pops the requeued song. `onBotVoiceStateChanged` stays as is too;
  the transient "left voice" during a rejoin is filtered in `CommandHandler`
  (below), so the controller never learns about the rejoiner.
- Update the header comment on `songInProgress` / `play()` if wording changes.

## 4. `VoiceRejoiner` (knows dpp, not the controller)

```cpp
// Re-establishes a voice connection DPP gave up on: leaves and rejoins the
// same channel, so the new session goes through IDENTIFY (works) instead of
// RESUME (DPP 10.1.6 never gets past it). Retries until the connection is
// ready again or it runs out of attempts.
class VoiceRejoiner
{
public:
    explicit VoiceRejoiner(dpp::cluster &bot_);

    void rejoin(uint64_t guildId, uint64_t channelId); // first attempt now, marks pending
    void onVoiceReady(uint64_t guildId);               // pending cleared
    void tick();                                       // from the existing 30 s timer: retry a stale pending rejoin
    bool isPending(uint64_t guildId);

private:
    void leaveAndJoin(uint64_t guildId, uint64_t channelId);

    dpp::cluster &bot;
    std::mutex mutex;
    struct Pending { uint64_t channelId{0}; int64_t lastAttemptAt{0}; int attempts{0}; };
    std::map<uint64_t, Pending> pending; // one guild in practice; the map keeps it honest

    static constexpr int64_t RETRY_AFTER_SECONDS = 30;
    static constexpr int MAX_ATTEMPTS = 5;
};
```

- `leaveAndJoin`: `dpp::discord_client *shard = bot.get_shard(0);` then
  `shard->disconnect_voice(guildId); shard->connect_voice(guildId, channelId);`
  Both are needed: `connect_voice` is a no-op while DPP still holds a
  `voiceconn` for the guild ("it seems we are already connected"), and
  `disconnect_voice` is what erases it. `disconnect_voice` sleeps 100 ms and
  destroys the client; by then no thread of ours touches it (the sender is
  gone and `play()` skipped its post-song calls). Log each attempt with
  `bot.log(dpp::ll_warning, ...)` so it shows next to DPP's own lines.
- `tick()`: for each pending entry older than `RETRY_AFTER_SECONDS`: retry if
  `attempts < MAX_ATTEMPTS`, else erase and log that it gave up (the queue
  stays; the user can `/join` by hand).
- All state under `mutex`: `rejoin` runs on the worker thread, `onVoiceReady`
  on a DPP event thread, `tick` on DPP's timer thread.
- The `dave` flag of `connect_voice` is left at its default. See the optional
  experiment at the end.

## 5. `CommandHandler` wiring

- Create `std::shared_ptr<VoiceRejoiner> rejoiner` when `bot` is non-null
  (member of `CommandHandler`, next to `playback`).
- `playback->setVoiceLostHandler([rejoiner](uint64_t guild, uint64_t channel) { rejoiner->rejoin(guild, channel); });`
- `on_voice_ready`: also `rejoiner->onVoiceReady(event.voice_client->server_id)`.
- `on_voice_state_update`, inside the existing `user_id == bot->me.id` branch:
  ```cpp
  // Our own leave during a rejoin is not a kick - don't wipe the queue.
  if (event.state.channel_id == 0 && rejoiner->isPending(event.state.guild_id)) return;
  ```
  before `playback->onBotVoiceStateChanged(...)`. (`voice_state_update_t::state`
  is a `dpp::voicestate` with `guild_id` and `channel_id`.)
- The existing 30-second timer: call `rejoiner->tick()` first, then the idle
  logic as today.

## 6. `PcmResampler::run` timing log

"Total decode/resample time" now includes the decoder blocking inside the
sink while the bounded queue is full, so it reads as the song length. Time the
`sink(...)` calls separately (accumulate around each call inside
`pushFullPackets` and the final drain) and subtract, then log three numbers:
read, decode/resample, waiting for the queue. Keep it to a few lines.

## DPP hazards to respect (all read from the 10.1.6 sources)

- DPP's retry uses `owner->start_timer([this](auto handle){...}, 1)` and the
  voice client destructor only calls `cleanup()`; a pending retry timer is not
  cancelled when the client is destroyed. So `disconnect_voice` while DPP is
  mid-retry can fire that timer on a dead object inside DPP. The 10 s
  watchdog makes this unlikely: DPP's chain either dies about 3 s in (what the
  report shows) or reaches its own full reconnection about 5 s in. This hazard
  also applies to a manual `/leave` today; nothing new, just don't lower the
  threshold.
- `terminating` is set at least 100 ms before destruction. The sender polls
  every 100 ms, which is the same bound DPP's own threads rely on.
- After `VoiceLost`, never call the client again from our code.
- A user `/pause` also stops the buffer draining. The watchdog treats a paused
  client as alive; verify with a pause longer than ten seconds.

## Commits (branch `voice-recovery` off master)

1. `PcmResampler`: split the timing log. Tiny, independent.
2. `VoiceDrainWatchdog` + `SongPlayer::Outcome` + sender integration. The
   controller adapts to the enum in this commit too, with `VoiceLost` handled
   like `Failed` for the moment so the build stays green.
3. `VoiceRejoiner` + the controller's voice-lost path + `CommandHandler`
   wiring. Add the two new files to `CMakeLists.txt`.

Commit messages: short header + `*` bullets, one line each, as short as
possible, ending with the co-author trailer of the model doing the work.
Comments only where really necessary and really short: the why, never the
what. Build with Qt Creator or the ninja command in the build dir; a
`LNK1168 cannot open discord_bot.exe` means the bot is still running.

## Test plan

1. Watchdog logic in isolation: a tiny harness (see the scratchpad harness
   pattern from earlier sessions, or a throwaway main) feeding
   `observe()` sequences: shrinking buffer never fires; constant buffer fires
   after 10 s; paused never fires; reset restarts the clock.
2. Reproduce the drop: with a long song playing, either suspend the bot
   process for about 90 s (Process Explorer > Suspend, or `pssuspend`) and
   resume it, or cut the network for 30 to 60 s. Discord drops the voice
   session; DPP starts its retry loop. Expected within about ten seconds of
   audio stopping: a "voice lost" line, the rejoiner's attempt line, DPP's
   "Connecting new voice session", then "Playing:" again and audio from the
   start of that song. `/queue` shows the rest of the queue intact. CPU back
   to idle.
3. `/pause` for 15 s then `/resume`: no voice-lost, playback continues.
4. Regression: play, skip, `/skip count:2`, stop, both loop modes,
   `/forward` inside and past the buffer, Ctrl+C mid-song.

## Optional experiment, not part of this work

`guild::connect_member_voice(..., dave = false)` joins without E2EE. If DPP's
RESUME works without DAVE, that says where its bug is. Trade-off: the channel
shows as not end-to-end encrypted while the bot is in it. The owner's call,
not a default to ship.
