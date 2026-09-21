# Rejoin handed channel 0, and command timing logs

Two small changes on branch `rejoin-channel-fix` off master, one commit each.

## 1. The recovery rejoined "channel 0" (log of 2026-09-17, 22:28:35)

`VoiceRejoiner` logged `leaving and rejoining channel 0`, five times, then gave
up. Joining channel 0 is a leave, so the recovery turned a lost session into a
permanent disconnect.

Cause, all in `PlaybackController`: `activeChannelId` is written in exactly two
places. `onVoiceReady` sets it, `stop()` zeroes it. `noteRequest()`, which runs
on `/play` and `/playlist` when the bot is already connected, adopts the voice
client pointer but not its channel. So join -> `/stop` -> `/play` leaves a
working session with `activeChannelId == 0` for good, and the voice-lost path
reports `activeGuildId, activeChannelId` to the handler. The same zero also
switches off `onBotVoiceStateChanged`'s `movedAway` check (it requires
`activeChannelId != 0`), so a kick or a drag goes unnoticed in that state.

Fix, three parts:

- In `playbackWorker`, where `voiceClient = currentVoiceClient` is taken under
  `stateMutex`, also capture `voiceClient->channel_id` and
  `voiceClient->server_id` into locals. The voice-lost report uses those, not
  `activeChannelId` / `activeGuildId`: the channel to rejoin is the one this
  song was playing in, whatever the bookkeeping says.
- In `noteRequest()`, when it adopts a ready client, also set
  `activeChannelId` and `activeGuildId` from it, so the session state is whole
  again after a `/stop` + `/play`.
- In `VoiceRejoiner::rejoin`, refuse `channelId == 0`: log a warning and do
  not mark anything pending. A recovery must never become a leave.

## 2. Log every slash command (so the next incident explains itself)

In the same session, after the incident, `/queue` (3 of 3) and `/stop` (1 of 1)
got Discord's "application did not respond" and the log shows four
`10062 Unknown interaction` errors, while `/skip`, `/leave`, `/join`, `/play`
and `/playlist` answered in time. The log has no line per command, so it can't
say whether the handlers started late, ran long, or whether the REST reply was
slow. Fix the blindness, not the symptom:

In `CommandHandler`'s `on_slashcommand` lambda, around `execute(event)`:

- at the start, one `qDebug` line with the command name and how old the
  interaction already is: `event.command.id.get_creation_time()` is Discord's
  creation time in seconds (a double); compare with
  `std::chrono::system_clock::now()`. Example:
  `/queue received 142 ms after it was issued`
- at the end, one line with the handler's own duration:
  `/queue handled in 3 ms`
- an unknown command name gets one line too.

Together with DPP's own `Error: 10062` lines and their timestamps, that tells
the three cases apart: late dispatch, slow handler, slow REST.

## Rules and checks

House rules: comments only where really necessary and really short (the why,
never the what); commit messages as a short header plus `*` bullets, one line
each, ending with the coding model's co-author trailer; nothing pushed; DPP is
not modified. Build after each commit, zero warnings; `LNK1168` at link only
means the owner's bot is running, then verify the link out of place.

Runtime check is the owner's: join, `/play`, `/stop`, `/play` again (the state
that broke it), then the suspend test; the rejoin line must name the real
channel id and playback must resume. And every command shows its two log lines.
