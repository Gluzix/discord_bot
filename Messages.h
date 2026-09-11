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

// playback errors
inline constexpr const char* errorResolve     = "Couldn't get the audio from that link :(";
inline constexpr const char* errorOpenStream  = "Couldn't open the audio stream :(";
inline constexpr const char* errorReadStream  = "Couldn't read the audio stream :(";
inline constexpr const char* errorNoAudio     = "That link has no audio stream :(";

// playback control policy
inline constexpr const char* mustBeWithBot        = "You need to be in my voice channel to control the music!";
inline constexpr const char* busyInChannelPrefix  = "I'm busy playing in ";
inline constexpr const char* busyInChannelSuffix  = " - join me there!";
inline constexpr const char* busyChannelFallback  = "another channel";

// /stop, /skip, /leave
inline constexpr const char* notConnected     = "I'm not connected to any voice channel!";
inline constexpr const char* nothingPlaying   = "Nothing is playing right now!";
inline constexpr const char* stoppedPlaying   = "Stopped playing and cleared the queue.";
inline constexpr const char* skipped          = "Skipped!";
inline constexpr const char* cannotLeave      = "Cannot leave, I'm not on the same channel as you!";
inline constexpr const char* leaving          = "Okay, i'm leaving :(";

// /queue
inline constexpr const char* queueEmpty            = "The queue is empty and nothing is playing.";
inline constexpr const char* queueNowPlayingPrefix = "Now playing: ";
inline constexpr const char* queueMorePrefix       = "...and ";
inline constexpr const char* queueMoreSuffix       = " more";

}
