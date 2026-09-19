#include "VoiceRejoiner.h"
#include "VoiceConnector.h"

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

uint64_t VoiceRejoiner::takeWaitForLeave(uint64_t guildId, int attempt)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto entry = pending.find(guildId);
    if (entry == pending.end() || !entry->second.waitingForLeave) {
        return 0;
    }
    // A timer armed by an older attempt must not end a newer attempt's wait.
    if (attempt != 0 && attempt != entry->second.attempts) {
        return 0;
    }
    entry->second.waitingForLeave = false;
    return entry->second.channelId;
}

void VoiceRejoiner::onBotLeft(uint64_t guildId)
{
    const uint64_t channelId = takeWaitForLeave(guildId, 0);
    if (channelId != 0) {
        join(guildId, channelId, "left");
    }
}

void VoiceRejoiner::onLeaveTimeout(uint64_t guildId, int attempt)
{
    const uint64_t channelId = takeWaitForLeave(guildId, attempt);
    if (channelId != 0) {
        join(guildId, channelId, "no leave confirmation after "
                                 + std::to_string(LEAVE_CONFIRM_SECONDS) + " s");
    }
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
        join(guildId, channelId, "not in voice");
        return;
    }

    int attempt = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto entry = pending.find(guildId);
        if (entry == pending.end()) {
            return;
        }
        entry->second.waitingForLeave = true;
        attempt = entry->second.attempts;
    }

    bot.log(dpp::ll_warning, "Voice rejoin: leaving guild " + std::to_string(guildId)
                             + " before rejoining channel " + std::to_string(channelId));
    shard->disconnect_voice(guildId);
    armLeaveTimeout(guildId, attempt);
}

// A gateway that died with the session swallows the leave, and then no
// confirmation ever comes; tick()'s retry is a minute away.
void VoiceRejoiner::armLeaveTimeout(uint64_t guildId, int attempt)
{
    std::weak_ptr<VoiceRejoiner> self = weak_from_this();
    // The cluster runs this callback, so it outlives the call.
    dpp::cluster *cluster = &bot;
    cluster->start_timer([self, cluster, guildId, attempt](dpp::timer handle) {
        cluster->stop_timer(handle); // one-shot
        if (std::shared_ptr<VoiceRejoiner> rejoiner = self.lock()) {
            rejoiner->onLeaveTimeout(guildId, attempt);
        }
    }, LEAVE_CONFIRM_SECONDS);
}

void VoiceRejoiner::join(uint64_t guildId, uint64_t channelId, const std::string &why)
{
    dpp::discord_client *shard = bot.get_shard(0);
    if (shard == nullptr) {
        return;
    }

    bot.log(dpp::ll_warning, "Voice rejoin: " + why + ", rejoining channel "
                             + std::to_string(channelId) + " in guild " + std::to_string(guildId));
    shard->connect_voice(guildId, channelId, voice::SELF_MUTE, voice::SELF_DEAF);
}
