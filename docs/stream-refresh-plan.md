# Say so when a song's stream dies

Branch `stream-lost-message` off master. One commit. Nothing more than this:
no retry, no url refresh, no compensation for the refusal itself. The owner's
call: the cause is upstream and gets fixed there; the bot only has to be
honest about what happened.

## The bug, from the logs

Three times in about fifty songs (2026-09-17 22:43:05 and 22:55:50, 2026-09-19
00:31:44) a song played for roughly fourteen seconds and then ended by itself,
which looks exactly like a phantom `/skip`. Each time the log says:

```
[decoder] Chunk fetch at 262144 failed: Server returned 403 Forbidden (access denied)
```

The first 256 KB chunk downloads, so the song opens, is announced and starts.
YouTube refuses the second range request. `ChunkedSource::read` returns `EIO`,
`av_read_frame` fails, and `PcmResampler::run` treats any negative return as
the end of the stream. The player plays out what it has and moves on without a
word. Likely cause, not proven and not ours: yt-dlp has no JavaScript runtime
installed and hands out urls from a fallback client without a proof-of-origin
token, which YouTube enforces sporadically.

## The change

1. `PcmResampler::run()` returns how it ended instead of `void`:
   `enum class RunEnd { EndOfStream, ReadError, Stopped };`
   `av_read_frame` returning `AVERROR_EOF` is `EndOfStream`, any other negative
   value is `ReadError`, `!keepGoing()` is `Stopped`. The epilogue (resampler
   drain, short last packet) runs for `EndOfStream` and `ReadError` alike, so
   whatever was decoded is still played.
2. `SongPlayer::decode`: when `run()` returns `ReadError`, post one NEW channel
   message, always `message_create` on the song's text channel, never an edit
   of the "Playing:" reply, with a new `messages::streamLost`, e.g.
   "Lost the audio stream for that song - moving on!", and
   `set_allowed_mentions()` like the other announcements. One `qDebug` line
   too, so the log has it in plain words.
   Post it once per song: the decoder's keep-alive loop may call `run()` again
   after a rewind, and a second failure must not post a second message.
3. `Messages.h`: the new string.

`play()`'s outcome stays `Finished`: the song did play, and a loop mode may try
it again later. `ChunkedSource` is not touched.

## Rules and checks

Comments only where really necessary and really short (the why, never the
what); commit message as a short header plus `*` bullets, one line each, ending
with the coding model's co-author trailer; nothing pushed; FFmpeg and DPP
untouched. Build with zero warnings; `LNK1168` at link only means the owner's
bot is running, then verify the link out of place.

A harness in the scratchpad linking the real `Playback/PcmResampler.cpp` and
`Playback/ChunkedSource.cpp` (FFmpeg + Qt, like the earlier `seek_test`) checks
`RunEnd`: a normal url decoded to its end gives `EndOfStream`; `keepGoing`
turning false gives `Stopped`; a read that fails mid-stream gives `ReadError`
with the already decoded audio still delivered. To provoke the failure without
waiting for YouTube: resolve a real url, let the first chunk be fetched, then
replace the source's `url` with a copy whose signature parameter is mangled
(googlevideo answers 403) before the second chunk is requested. The source's
members are public, so the harness can do that through a small test-only
accessor or by opening with the good url and swapping afterwards; do not add
production API just for the test.
