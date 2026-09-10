#pragma once

namespace dpp {
struct slashcommand_t;
}

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
    // voice channel. Does not reply to the interaction.
    static Result ensureJoined(const dpp::slashcommand_t &event);
};
