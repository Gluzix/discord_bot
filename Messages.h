#pragma once

// Every reply the bot sends to users, in one place - edit the bot's voice
// here. Log messages and technical strings stay next to their code.
namespace messages {

// joining voice channels
inline constexpr const char* joinedChannel    = "Joined your channel!";
inline constexpr const char* alreadyInChannel = "Don't need to join your channel as i'm already there with you!";
inline constexpr const char* userNotInVoice   = "You don't seem to be in a voice channel!";

// /play
inline constexpr const char* lookingForSong   = "Looking for your song...";
inline constexpr const char* invalidSongInput = "Give me a YouTube link or a song title to search for!";
inline constexpr const char* queuedAtPrefix   = "Queued at position ";
inline constexpr const char* queuedSeparator  = ": ";
inline constexpr const char* playingPrefix    = "Playing: ";
inline constexpr const char* searchLabelPrefix = "search: ";
inline constexpr const char* usePlaylistCommand = "That's a playlist - use /playlist for it!";

// /playlist
inline constexpr const char* invalidPlaylistInput    = "Give me a YouTube playlist link (one with list= in it)!";
inline constexpr const char* readingPlaylist         = "Reading the playlist...";
inline constexpr const char* playlistEmpty           = "Couldn't find any playable videos in that playlist :(";
inline constexpr const char* playlistQueuedPrefix    = "Queued ";
inline constexpr const char* playlistQueuedOne       = " song from ";
inline constexpr const char* playlistQueuedMany      = " songs from ";
inline constexpr const char* playlistFallbackName    = "the playlist";
inline constexpr const char* playlistTruncatedPrefix = " (it has ";
inline constexpr const char* playlistTruncatedSuffix = " - I took the first ones)";

// playback errors
inline constexpr const char* errorResolve     = "Couldn't get the audio from that link :(";
inline constexpr const char* errorOpenStream  = "Couldn't open the audio stream :(";
inline constexpr const char* errorReadStream  = "Couldn't read the audio stream :(";
inline constexpr const char* errorNoAudio     = "That link has no audio stream :(";
inline constexpr const char* streamLost       = "Lost the audio stream for that song - moving on!";

// playback control policy
inline constexpr const char* mustBeWithBot        = "You need to be in my voice channel to control the music!";
inline constexpr const char* busyInChannelPrefix  = "I'm busy playing in ";
inline constexpr const char* busyInChannelSuffix  = " - join me there!";
inline constexpr const char* busyChannelFallback  = "another channel";

// /stop, /skip, /leave /pause
inline constexpr const char* notConnected     = "I'm not connected to any voice channel!";
inline constexpr const char* nothingPlaying   = "Nothing is playing right now!";
inline constexpr const char* stoppedPlaying   = "Stopped playing and cleared the queue.";
inline constexpr const char* skipped          = "Skipped!";
inline constexpr const char* skippedManyPrefix = "Skipped ";
inline constexpr const char* skippedManySuffix = " songs!";
inline constexpr const char* cannotLeave      = "Cannot leave, I'm not on the same channel as you!";
inline constexpr const char* leaving          = "Okay, i'm leaving :(";
inline constexpr const char* idleLeft         = "Nothing played for a while - I'm leaving the voice channel!";
inline constexpr const char* pause            = "Pausing currently playing song";
inline constexpr const char* resume           = "Resuming currently paused song";
inline constexpr const char* nothingToResume   = "Nothing to resume";

// /forward, /rewind, /seek
inline constexpr const char* forwardedTo      = "Forwarded to ";
inline constexpr const char* rewoundTo        = "Rewound to ";
inline constexpr const char* jumpedTo         = "Jumped to ";
inline constexpr const char* songStillLoading = "That song is still loading - try again in a moment!";
inline constexpr const char* invalidPosition  = "Give me a position like 90, 1:30 or 1:02:03!";

// /loop
inline constexpr const char* loopSong  = "Looping the current song!";
inline constexpr const char* loopQueue = "Looping the whole queue!";
inline constexpr const char* loopOff   = "Looping turned off!";

// /queue
inline constexpr const char* queueEmpty            = "The queue is empty and nothing is playing.";
inline constexpr const char* queueLoopSongSuffix   = " (loop: song)";
inline constexpr const char* queueLoopQueueSuffix  = " (loop: queue)";
inline constexpr const char* queueNowPlayingPrefix = "Now playing: ";
inline constexpr const char* queueMorePrefix       = "...and ";
inline constexpr const char* queueMoreSuffix       = " more";

}
