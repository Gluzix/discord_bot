#pragma once

namespace dpp {
struct slashcommand_t;
struct interaction_create_t;
}

class PlaybackController;

// The bot never listens, so it joins deafened - Discord then sends it nobody's
// audio. Both join paths share these so a rejoin can't un-deafen it.
namespace voice {
inline constexpr bool SELF_MUTE = false;
inline constexpr bool SELF_DEAF = true;
}

// Voice-channel join logic shared by the commands; pure logic, no replies.
// =======================================================
// Rules:
// - A move connects straight to the new channel in ensureJoined(), never
//   disconnect first: dpp replaces the voiceconn, and a disconnect+connect
//   races the gateway - the disconnect's ack tears down the new connection
//   and voice_ready never fires.
// - The switch still destroys the old voice client, so playback, whose
//   threads hold a pointer to it, stops first (playback->stop() in
//   ensureJoined()).
// =======================================================
class VoiceConnector
{
public:
    enum class Result {
        Joined,           // started connecting to the user's channel
        AlreadyInChannel, // nothing to do, bot is already with the user
        UserNotInVoice,   // user isn't in any voice channel - no connection is coming
    };

    // Makes sure the bot is connected (or connecting) to the issuing user's
    // voice channel. Does not reply to the interaction. Stops playback
    // before switching channels.
    static Result ensureJoined(const dpp::slashcommand_t &event, PlaybackController *playback);

    // True when the bot has a voice connection and the issuing user is in
    // that same channel - the requirement for controlling active playback.
    static bool userInBotChannel(const dpp::interaction_create_t &event);

    // True when nobody (except the bot itself) is in the bot's channel.
    // An audience of zero deserves no loyalty - anyone may take the bot.
    static bool botIsAloneInChannel(const dpp::interaction_create_t &event);
};
