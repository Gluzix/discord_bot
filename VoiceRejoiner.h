#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace dpp {
class cluster;
}

// Re-establishes a voice connection dpp gave up on by leaving and rejoining.
// =======================================================
// Rules:
// - Leaving first, through leave() from rejoin() and tick(), is what makes
//   this work: connect_voice does nothing while dpp still holds a voiceconn
//   for the guild, and the new session goes through IDENTIFY (works) instead
//   of RESUME (dpp 10.1.6 never gets past it).
// - The leave and the join are separate phases: dpp erases the guild's
//   voiceconn when Discord confirms the leave, and a join sent earlier dies
//   with it (leave(), then onBotLeft() or onLeaveTimeout()).
// - rejoin() refuses channel 0: joining channel 0 is a leave, and a recovery
//   must never become one.
// - dpp is called with no lock held, in every method: its voice teardown is
//   slow and its events come back on other threads.
// - Held by a shared_ptr made in CommandHandler::prepare():
//   armLeaveTimeout()'s leave timeout runs on a dpp timer thread.
// =======================================================
class VoiceRejoiner : public std::enable_shared_from_this<VoiceRejoiner>
{
public:
    explicit VoiceRejoiner(dpp::cluster &bot_);

    // First attempt right away; the guild stays pending until it is ready.
    void rejoin(uint64_t guildId, uint64_t channelId);

    // From the bot's own voice state going empty: the leave we asked for is
    // confirmed, so the join can go out now.
    void onBotLeft(uint64_t guildId);

    // A user's /leave: the bot was told to go, so stop trying to come back.
    void cancel(uint64_t guildId);

    void onVoiceReady(uint64_t guildId);

    // From the bot's periodic timer: retries a pending rejoin that went stale.
    void tick();

    bool isPending(uint64_t guildId);

private:
    void leave(uint64_t guildId, uint64_t channelId);
    void join(uint64_t guildId, uint64_t channelId, const std::string &why);
    void armLeaveTimeout(uint64_t guildId, int attempt);
    void onLeaveTimeout(uint64_t guildId, int attempt);

    // The confirmation and the timeout race for the join; this lets the first
    // one through and leaves the other nothing to do. Returns the channel to
    // join, or 0. attempt 0 matches any, otherwise only that attempt's timer.
    uint64_t takeWaitForLeave(uint64_t guildId, int attempt);

    dpp::cluster &bot;
    std::mutex mutex;
    struct Pending {
        uint64_t channelId{0};
        int64_t lastAttemptAt{0};
        int attempts{0};
        bool waitingForLeave{false}; // the leave went out, Discord hasn't confirmed it
    };
    std::map<uint64_t, Pending> pending; // one guild in practice; the map keeps it honest

    static constexpr int64_t RETRY_AFTER_SECONDS = 30;
    static constexpr uint64_t LEAVE_CONFIRM_SECONDS = 5;
    static constexpr int MAX_ATTEMPTS = 5;
};
