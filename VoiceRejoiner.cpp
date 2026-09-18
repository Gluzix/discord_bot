#include "VoiceRejoiner.h"

#include <dpp/dpp.h>

#include <ctime>
#include <string>
#include <utility>
#include <vector>

VoiceRejoiner::VoiceRejoiner(dpp::cluster &bot_)
    : bot(bot_)
{
}

void VoiceRejoiner::rejoin(uint64_t guildId, uint64_t channelId)
{
    // Joining channel 0 is a leave - a recovery must never become one.
    if (channelId == 0) {
        bot.log(dpp::ll_warning, "Voice rejoin for guild " + std::to_string(guildId)
                                 + " skipped: the channel is unknown");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        Pending &entry = pending[guildId];
        entry.channelId = channelId;
        entry.lastAttemptAt = static_cast<int64_t>(time(nullptr));
        entry.attempts = 1;
        entry.waitingForLeave = false;
    }
    leave(guildId, channelId);
}

void VoiceRejoiner::onBotLeft(uint64_t guildId)
{
    uint64_t channelId = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto entry = pending.find(guildId);
        if (entry == pending.end() || !entry->second.waitingForLeave) {
            return;
        }
        entry->second.waitingForLeave = false;
        channelId = entry->second.channelId;
    }
    join(guildId, channelId);
}

void VoiceRejoiner::cancel(uint64_t guildId)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.erase(guildId) == 0) {
            return;
        }
    }
    bot.log(dpp::ll_warning, "Voice rejoin for guild " + std::to_string(guildId)
                             + " cancelled: the bot was asked to leave");
}

void VoiceRejoiner::onVoiceReady(uint64_t guildId)
{
    std::lock_guard<std::mutex> lock(mutex);
    pending.erase(guildId);
}

void VoiceRejoiner::tick()
{
    std::vector<std::pair<uint64_t, uint64_t>> retries; // guild, channel
    std::vector<uint64_t> gaveUp;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const int64_t now = static_cast<int64_t>(time(nullptr));
        for (auto it = pending.begin(); it != pending.end();) {
            if (now - it->second.lastAttemptAt < RETRY_AFTER_SECONDS) {
                ++it;
            } else if (it->second.attempts >= MAX_ATTEMPTS) {
                gaveUp.push_back(it->first);
                it = pending.erase(it);
            } else {
                it->second.lastAttemptAt = now;
                ++it->second.attempts;
                // A leave confirmation that never came must not join later.
                it->second.waitingForLeave = false;
                retries.emplace_back(it->first, it->second.channelId);
                ++it;
            }
        }
    }

    // dpp is called with no lock held - its voice teardown is slow and its
    // events come back on other threads.
    for (uint64_t guildId : gaveUp) {
        bot.log(dpp::ll_warning, "Voice rejoin for guild " + std::to_string(guildId)
                                 + " gave up; the queue is kept, /join to retry");
    }
    for (const std::pair<uint64_t, uint64_t> &retry : retries) {
        leave(retry.first, retry.second);
    }
}

bool VoiceRejoiner::isPending(uint64_t guildId)
{
    std::lock_guard<std::mutex> lock(mutex);
    return pending.find(guildId) != pending.end();
}

// Leaving first is what makes this work: connect_voice does nothing while dpp
// still holds a voiceconn for the guild.
void VoiceRejoiner::leave(uint64_t guildId, uint64_t channelId)
{
    dpp::discord_client *shard = bot.get_shard(0);
    if (shard == nullptr) {
        return;
    }

    // Nothing to leave means no confirmation will ever come.
    if (shard->get_voice(guildId) == nullptr) {
        join(guildId, channelId);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        auto entry = pending.find(guildId);
        if (entry == pending.end()) {
            return;
        }
        entry->second.waitingForLeave = true;
    }

    bot.log(dpp::ll_warning, "Voice rejoin: leaving guild " + std::to_string(guildId)
                             + " before rejoining channel " + std::to_string(channelId));
    shard->disconnect_voice(guildId);
}

void VoiceRejoiner::join(uint64_t guildId, uint64_t channelId)
{
    dpp::discord_client *shard = bot.get_shard(0);
    if (shard == nullptr) {
        return;
    }

    bot.log(dpp::ll_warning, "Voice rejoin: left, rejoining channel "
                             + std::to_string(channelId) + " in guild " + std::to_string(guildId));
    shard->connect_voice(guildId, channelId);
}
