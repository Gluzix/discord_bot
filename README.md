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
| `/join` / `/leave` | Summon the bot to your voice channel, or send it away. |

A few behaviours worth knowing:

- **The audience is in charge.** Once the bot is playing for people, only users in its voice channel can control it or call it to another channel. An idle bot follows anyone.
- **It leaves when unused.** After five minutes with nothing playing and an empty queue, the bot leaves the voice channel.
- **Long sessions are fine.** Queued songs are resolved a few ahead of time so transitions are gapless, and a looping song re-resolves itself before its stream URL expires.
- **Ctrl+C is graceful.** The bot shuts down its threads and disconnects cleanly.

## How it works

```
slash command ──► Commands/*Command ──► PlaybackController
                                             │  song queue + worker thread
                        yt-dlp (process) ◄───┤  resolver thread: title + direct url
                                             ▼
                                      PcmResampler (FFmpeg) ──► PCM packets ──► sender thread ──► DPP ──► Discord
```

- `Commands/` holds one small class per slash command. `CommandHandler` wires them up, registers them with Discord, and owns the idle timer.
- `PlaybackController` owns the queue and the threads. A persistent worker plays songs one after another; a resolver thread runs yt-dlp ahead of time so the next song starts without a pause.
- `PcmResampler` turns a media URL into 48 kHz 16-bit stereo PCM using FFmpeg, in packets of exactly the size DPP wants. It knows nothing about threads or Discord.
- `WindowsProcessRunner` runs yt-dlp and parses its output. `VoiceConnector` handles joining channels and the audience rule. `Labels` and `Messages` hold every string the user sees.

## Requirements

- Windows 10 or 11, Visual Studio 2022 (MSVC), CMake 3.14+, Ninja (Qt Creator provides one).
- Qt 6 (only `Core` is used).
- FFmpeg via [vcpkg](https://vcpkg.io): `vcpkg install ffmpeg:x64-windows`.
- [DPP 10.1.6](https://github.com/brainboxdotcc/DPP/releases) prebuilt package for Windows, unpacked **next to** this repository as `libdpp-10.1.6-win64-debug-vs2022` (the CMake file points there).
- [yt-dlp](https://github.com/yt-dlp/yt-dlp/releases) on your `PATH`, kept up to date. An outdated yt-dlp is the usual reason for a sudden "Couldn't get the audio" on every song.

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

## Development

Pull requests are reviewed automatically by a Claude workflow in `.github/workflows/claude-review.yml`; it posts one summary comment per PR. Feature work happens on branches and lands through PRs.

The codebase carries a few hard-won rules that are easy to break by accident:

- Audio packets handed to DPP must be exactly `dpp::send_audio_raw_max_length` bytes; DPP silently drops the tail of anything larger.
- Never call into DPP while holding one of the bot's own mutexes, and never touch the voice client from a thread other than the one that owns it at that moment.
- Bugs that live in DPP itself are documented in commit messages and worked around in this repo, not patched in DPP.

## Known limitations

- One voice session at a time. The bot is built for a single server.
- If yt-dlp is not on `PATH`, the fallback path in `WindowsProcessRunner.cpp` is the author's; change it or, better, put yt-dlp on your `PATH`.
- Playlists are capped at 100 entries.
- No seeking backwards yet; `/forward` only moves ahead.

## License

No license has been chosen yet. Until one is added, all rights are reserved by the author.
