#pragma once

#include <cstdint>
#include <map>
#include <mutex>

namespace dpp {
class cluster;
}

// Re-establishes a voice connection dpp gave up on: leaves and rejoins the
// same channel, so the new session goes through IDENTIFY (works) instead of
// RESUME (dpp 10.1.6 never gets past it). Retries until the connection is
// ready again or it runs out of attempts.
class VoiceRejoiner
{
public:
    explicit VoiceRejoiner(dpp::cluster &bot_);

    // First attempt right away; the guild stays pending until it is ready.
    void rejoin(uint64_t guildId, uint64_t channelId);

    void onVoiceReady(uint64_t guildId);

    // From the bot's periodic timer: retries a pending rejoin that went stale.
    void tick();

    bool isPending(uint64_t guildId);

private:
    void leaveAndJoin(uint64_t guildId, uint64_t channelId);

    dpp::cluster &bot;
    std::mutex mutex;
    struct Pending { uint64_t channelId{0}; int64_t lastAttemptAt{0}; int attempts{0}; };
    std::map<uint64_t, Pending> pending; // one guild in practice; the map keeps it honest

    static constexpr int64_t RETRY_AFTER_SECONDS = 30;
    static constexpr int MAX_ATTEMPTS = 5;
};
