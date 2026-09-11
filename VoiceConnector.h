#pragma once

namespace dpp {
struct slashcommand_t;
}

class PlaybackController;

// Voice-channel join logic shared by /join and /play - extracted so that
// PlayCommand no longer has to inherit from JoinCommand just to reuse it.
// Pure logic, no replies: each command decides how to talk about the result.
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
    // before switching channels - the old voice client is destroyed by the
    // switch and no thread may keep using it.
    static Result ensureJoined(const dpp::slashcommand_t &event, PlaybackController *playback);

    // True when the bot has a voice connection and the issuing user is in
    // that same channel - the requirement for controlling active playback.
    static bool userInBotChannel(const dpp::slashcommand_t &event);

    // True when nobody (except the bot itself) is in the bot's channel.
    // An audience of zero deserves no loyalty - anyone may take the bot.
    static bool botIsAloneInChannel(const dpp::slashcommand_t &event);
};
