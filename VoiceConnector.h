#pragma once

namespace dpp {
struct slashcommand_t;
}

// Voice-channel join logic shared by /join and /play - extracted so that
// PlayCommand no longer has to inherit from JoinCommand just to reuse it.
class VoiceConnector
{
public:
    // Makes sure the bot is connected (or connecting) to the issuing user's
    // voice channel. Replies to the interaction in every branch, so callers
    // can rely on the interaction being acknowledged afterwards.
    // Returns false when the user isn't in a voice channel - in that case
    // no voice connection exists or will be coming.
    static bool ensureJoined(const dpp::slashcommand_t &event);
};
