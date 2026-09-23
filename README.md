# Discord music bot written in C++

A Discord music bot in C++ that streams YouTube audio into a voice channel. Paste a link, a playlist, or just a song title, and it plays.

Built on [D++ (DPP)](https://dpp.dev) for Discord, FFmpeg for decoding, [yt-dlp](https://github.com/yt-dlp/yt-dlp) for resolving YouTube, and Qt Core for the small amount of plumbing. Windows only for now.

## Commands

| Command | What it does |
| --- | --- |
| `/play song:<link or title>` | Plays a YouTube link, or searches YouTube for a title and plays the first hit. Queues it if something is already playing. |
| `/playlist link:<playlist link>` | Queues every video of a YouTube playlist (the first 100). |
| `/queue` | Shows what's playing and what's waiting. |
| `/skip [count]` | Skips the current song, or the current one plus the next `count-1`. |
| `/stop` | Stops playback and clears the queue. |
| `/pause` / `/resume` | What they say. |
| `/loop mode:<song\|queue\|off>` | Repeat the current song, rotate the whole queue, or stop looping. |
| `/forward [seconds]` | Jumps ahead in the current song (default 10, up to 600). |
| `/rewind [seconds]` | Jumps back in the current song (default 10, up to 600). |
| `/seek position:<90 \| 1:30 \| 1:02:03>` | Jumps to a position in the current song. |
| `/join` / `/leave` | Summon the bot to your voice channel, or send it away. |
| `/ping` | Replies "ping test"; a quick check that the bot is up. |

Every "Playing:" message carries a row of buttons: rewind 10 s, play/pause, forward 10 s and skip. They do what the matching commands do, to whatever is playing now and under the same rules. A click's answer becomes a status line under the song's title for everyone to see; a refusal is shown only to whoever clicked.

A few behaviours worth knowing:

- **The audience is in charge.** Once the bot is playing for people, only users in its voice channel can control it or call it to another channel. An idle bot follows anyone.
- **It never listens.** The bot joins deafened, so Discord sends it nobody's audio.
- **It leaves when unused.** After five minutes with nothing playing and an empty queue, the bot leaves the voice channel.
- **Long sessions are fine.** Queued songs are resolved a few ahead of time so transitions are gapless, and a looping song re-resolves itself before its stream URL expires.
- **A network outage doesn't eat the queue.** One failed song is dropped with a message, but when the next one fails too, the bot keeps that one, says so once, and retries it every 30 seconds, up to 5 times, before moving on. `/skip` moves past a held song at once.
- **A dropped voice connection comes back.** If it dies mid-song, the bot leaves and rejoins its channel on its own (up to 5 tries, at least 30 seconds apart) and starts that song again. The connection is also checked before every song, so one that died while idle is rebuilt first.
- **Ctrl+C is graceful.** The bot shuts down its threads and disconnects cleanly.

## How it works

```
slash command ──► Commands/*Command ──► PlaybackController
                                             │  song queue + worker thread
                        yt-dlp (process) ◄───┤  resolver thread: title + direct url
                                             ▼  SongPlayer: one song at a time
                      decoder thread: PcmResampler (FFmpeg) ──► PcmBuffer ──► sender thread ──► DPP ──► Discord
```

- `Commands/` holds one small class per slash command. `CommandHandler` wires them up, registers them with Discord, and owns the idle timer.
- `ButtonRouter` takes a click on a "Playing:" button to the command behind it, and `PlaybackButtons` builds that row and reads its ids. Those commands reply through `Interactions`, which puts a click's answer on the clicked message itself and shows a refusal to the clicker alone.
- `PlaybackController` owns the queue and the worker thread, which hands songs to `SongPlayer` one after another; `ResolverWorker` runs yt-dlp ahead of time on its own thread so the next song starts without a pause.
- `SongPlayer` commands one song. `DecoderWorker` resolves, announces and decodes it on one thread, `SenderWorker` paces the packets into the voice client on another, and `PcmBuffer` sits between them: the bounded PCM queue plus the seek handshake.
- `PcmResampler` turns a media URL into 48 kHz 16-bit stereo PCM using FFmpeg, in packets of exactly the size DPP wants. It knows nothing about threads or Discord.
- `ChunkedSource` fetches the stream for FFmpeg in bounded ranges: googlevideo serves those at full speed but throttles an open-ended read to about twice the audio bitrate.
- `WindowsProcessRunner` runs yt-dlp and parses its output. `VoiceConnector` handles joining channels and the audience rule. `Labels` and `Messages` hold every reply the user sees.
- `VoiceDrainWatchdog` spots a voice client that stopped draining, and `VoiceRejoiner` replaces a lost voice session by leaving and rejoining the channel. `FailureStreak` tells a broken song from a broken network: one failure drops the song, failures back to back keep it for a retry.
- `CtrlMainServer` starts the bot with the token `TokenReader` reads from `token.json`, and shuts it down on Ctrl+C. `Log` writes the log files, `TimeText` reads and prints song positions, and `YoutubeInput` keeps user input to plain YouTube links and tame search text before it reaches the yt-dlp command line.

## Requirements

- Windows 10 or 11, Visual Studio 2022 (MSVC), CMake 3.14+, Ninja (Qt Creator provides one).
- Qt 6 (only `Core` is used).
- FFmpeg via [vcpkg](https://vcpkg.io): `vcpkg install ffmpeg:x64-windows`.
- [DPP 10.1.6](https://github.com/brainboxdotcc/DPP/releases) prebuilt package for Windows, unpacked **next to** this repository as `libdpp-10.1.6-win64-debug-vs2022` (the CMake file points there).
- [yt-dlp](https://github.com/yt-dlp/yt-dlp/releases) on your `PATH` (or point `YT_DLP_PATH` at the binary), kept up to date. An outdated yt-dlp is the usual reason for a sudden "Couldn't get the audio" on every song.

## Building

Open `CMakeLists.txt` in Qt Creator with the MSVC 2022 kit and build, or from a Developer Command Prompt:

```bash
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

```bash
cmake --build build
```

The build copies `dpp.dll` and `cacert.pem` next to the executable. The CA bundle matters: DPP verifies TLS certificates through OpenSSL, which has no CA store on Windows by default, so without it every connection fails with "Malformed HTTP response".

## Configuration

Create a Discord application with a bot user, enable it for your server with the `applications.commands` scope and voice permissions, and put its token in a file named `token.json` in the directory the bot runs from (the build directory when launched from Qt Creator):

```json
{ "discord_token": "your-bot-token" }
```

`token.json` is ignored by git. Keep it that way; this is a public repository.

Slash commands are registered globally on startup, so they appear in every server the bot is in after a short delay.

## Logs

The bot logs to the console and to `logs/discord_bot-<date>-<time>.log` in the directory it runs from, a new file for every start. DPP's websocket trace goes to the file only. A file that reaches 10 MB is closed and a fresh one opened, and the oldest files are deleted once all of them together pass 50 MB.

## Development

Pull requests are reviewed automatically by a Claude workflow in `.github/workflows/claude-review.yml`; it comments on the whole diff when a PR is opened, then on just the new commits after every push to it. Feature work happens on branches and lands through PRs.

The codebase carries a few hard-won rules that are easy to break by accident:

- Audio packets handed to DPP must be exactly `dpp::send_audio_raw_max_length` bytes; DPP silently drops the tail of anything larger.
- Never call into DPP while holding one of the bot's own mutexes, and never touch the voice client from a thread other than the one that owns it at that moment.
- Bugs that live in DPP itself are documented in commit messages and worked around in this repo, not patched in DPP.

Rules that belong to one class or namespace live in a rules block at the top of its header.

## Known limitations

- One voice session at a time. The bot is built for a single server.
- Playlists are capped at 100 entries.

## License

No license has been chosen yet. Until one is added, all rights are reserved by the author.
