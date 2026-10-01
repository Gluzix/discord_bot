# What it takes to raise the playlist cap

Status: findings only, from 2026-09-29. Nothing was changed; `/playlist`
still takes the first 100 videos (`MAX_ENTRIES` in
`Commands/PlaylistCommand.cpp`). The owner has no need for more today.

Every number below was measured by one reader and measured again by a
skeptic, on this PC (i9-13900K), with the debug DPP package the bot is
built against and yt-dlp 2026.08.19. Timings of yt-dlp are from one
evening and one network.

## The short answer

| Limit | Listing | Memory while queued | Longest lock hold | Verdict |
|---|---|---|---|---|
| 100 (today) | 2 s, once 10 s | 1.6 MB | 1.5 ms | fine |
| 500 | 4-6 s | 8 MB | 7 ms | change the number and the texts |
| 1000 | 7-11 s | 16 MB | 14 ms, 46 ms when other threads allocate | change the number and the texts |
| 5000 | 27-37 s | 78 MB | 70 ms, up to 230 ms when other threads allocate | needs the changes below first |
| none | over 60 s for an uploads playlist of 20000 | not bounded | not bounded | not possible as the code is |

"The texts" are `README.md` (two sentences name the 100), the comment
above `LOOKAHEAD` in `Playback/ResolverWorker.cpp`, and two comments that
say a listing takes "a few seconds" (`Resolver/YtDlpResolver.cpp`,
`Commands/PlaylistCommand.cpp`).

The cap is per `/playlist` command. Nothing bounds the queue itself:
commands add up.

## What a queued song costs

- Every song owns a full copy of the `/playlist` event
  (`std::make_unique<dpp::slashcommand_t>(event)` in `playPlaylist()`).
  That copy is 15 of the 16 KB a song costs. A channel with a long topic
  makes it about 2 KB more.
- With one event shared by all songs of a playlist, a song costs 1 KB:
  5 MB for 5000 songs.
- The copies are made while `stateMutex` is held, and destroyed under it
  in `stop()`: clearing 5000 songs takes 44-52 ms, up to 195 ms when other
  threads allocate.
- The memory comes back when the queue empties.
- A release build of DPP would make all of this 2 to 7 times cheaper. The
  bot links the debug package.

## Why the lock hold matters

The leave timer runs on DPP's event-loop thread and takes `stateMutex`
(`VoiceLeaver::tick()` -> `idleInfo()`). The same thread sends every voice
packet. When a timer tick falls into a long hold, voice sending stops for
the rest of the hold. At 1000 songs that is below one 20 ms frame; at
5000 it is 3 to 11 frames. The chance is small (the hold divided by
30 s), and whether it is heard was not measured.

The audio sender itself never takes `stateMutex`.

## What the listing does

- yt-dlp fetches pages of 100 and only as many as it needs: about 2.5 s
  fixed, then about 0.5 s per page.
- It prints nothing until it has fetched everything. A timeout during the
  fetch leaves no entries at all, and the bot then answers "Couldn't find
  any playable videos in that playlist :(".
- The timeout is 60 s and is shared with song resolving
  (`YT_DLP_TIMEOUT_SECONDS`). A listing reaches it at about 8500 to 12000
  entries.
- With `--lazy-playlist` yt-dlp prints page by page, so a timeout keeps
  what has arrived. The playlist's title and count still come last, so
  they are missing then.
- The bot cannot tell a timeout from a finished listing: neither
  `IProcessRunner::Output` nor `PlaylistListing` has a field for it.
- The listing holds one of DPP's pool threads until it ends, and it cannot
  be cancelled: a `/stop` during the listing is followed by the playlist
  being queued.
- The runner and the pipe set no limit: 10000 lines are fine.

## Changes a limit in the thousands needs first

1. One shared event per playlist
   (`std::shared_ptr<const dpp::slashcommand_t>`). Places that create,
   copy or bind the event today: `Playback/Song.h:17`,
   `PlaybackController.cpp:56`, `:80`, `:554`, `ResolverWorker.cpp:85`,
   `:112`, `DecoderWorker.cpp:106`, `:168` (the last two bind it
   non-const).
2. A listing that survives a timeout: `--lazy-playlist`, a timed-out flag
   in `IProcessRunner::Output` and `PlaylistListing`, and a message that
   says so.
3. The note "(it has N - I took the first ones)" compares the playlist's
   size with the number of songs queued. It must ask whether the limit was
   reached instead (see below).
4. A decision on mixes (see below).
5. A decision on `/skip`: it takes at most 100 songs per command, so
   5000 songs are 50 commands or `/stop`.
6. A decision on the failure hold: with a broken yt-dlp a queue of 5000
   means error messages for up to 8.7 days (100 songs: 4 hours).
7. `queueSnapshot()` renders a label for every waiting song although
   `/queue` shows 15: 10 ms at 5000, under the lock.

Tests that pin today's behaviour: `tests/YtDlpResolverTest.cpp` (the
arguments with `:100`, the 60 s, "NA titles are left out") and
`tests/WindowsCommandLineTest.cpp` (the `:100` command line). Changing only
`MAX_ENTRIES` breaks none of them. No test covers `PlaylistCommand` or
`playPlaylist()`.

## Found on the way, true at 100 as well

These do not depend on the cap. None is fixed.

- **A title "NA" can be a hiccup of YouTube.** In one of five large runs,
  38 videos of one page came back with the title "NA"; a minute later
  they had their titles. The bot takes "NA" for a private video and leaves
  the song out.
- **The note about taking "the first ones" can be untrue.** It appears
  whenever fewer songs were queued than the playlist has, also when songs
  were left out and nothing was cut.
- **Mixes repeat songs.** A link with `list=RD...` is accepted as a
  playlist. In the two long mix listings, repeats start at position 26,
  and 18 and 20 of the first 100 entries are repeats; the one listing
  made with the bot's own arguments had none. One mix ended by itself at
  415 entries, another at 1177, of which 357 were different songs.
- **A `/play` far back in the queue loses its second answer.** "Queued
  at position N: title" is an edit through the interaction token, which
  Discord keeps for 15 minutes. The edit is sent when the song comes
  among the first five waiting, so it fails whenever that is later.
  Nothing logs it: the edit has no callback.
- **A timeout of the listing looks like an empty playlist.**
- **A queue that survives a failed rejoin is never freed** until someone
  uses `/join` or `/stop`: the idle timer waits for an empty queue, and
  the alone timer for a voice channel.
- **The comment "even a 6000-video playlist answers in a few seconds"**
  in `Resolver/YtDlpResolver.cpp` is not true: 5000 took 27 to 37 s.

The announcements of playlist songs are safe for any queue length: they
go out as channel messages with the bot's own token.

## Not measured

- The size of a real interaction frame (the probes used invented ones of
  about 2 KB).
- The lock hold inside the running bot; the probes ran alone.
- Whether a stall of DPP's event loop of 14 to 230 ms is heard.
- Whether the playlist's count includes private and deleted videos.
- What yt-dlp does when one page fails in the middle of a long listing.
- A real kill at 60 s in the middle of the printing.

The probes and the raw yt-dlp output are in the session's scratch folder
(`playlist-cap\`), which does not last.
