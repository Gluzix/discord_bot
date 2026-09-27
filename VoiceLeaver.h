#pragma once

#include "PlaybackController.h"
#include "EmptyRoomClock.h"

#include <cstdint>
#include <memory>

namespace dpp {
class cluster;
}

class VoiceRejoiner;

// Leaves the voice channel once the bot has been idle or alone for too long.
// =======================================================
// Rules:
// - Held by a shared_ptr that the timer callback of
//   CommandHandler::setupBot() keeps: tick() needs nothing of
//   CommandHandler, whatever thread dpp ticks on and whenever that class
//   goes.
// - dpp's shard registry is the only truth about whether the bot is in
//   voice: it is asked in leaveIfIdle() and VoiceConnector::botChannel(),
//   never tracked here.
// - leaveVoice() calls playback->stop() before it disconnects, so our
//   threads let go of the voice client first, and cancels the pending
//   rejoin, or the rejoiner would bring the bot straight back.
// - emptyRoomClock has no lock: only tick() touches it, and dpp runs the
//   ticks of a timer one after the other.
// =======================================================
class VoiceLeaver
{
public:
    // Reads BOT_STAY_WHEN_ALONE, once.
    VoiceLeaver(dpp::cluster &bot_, std::shared_ptr<PlaybackController> playback_,
                std::shared_ptr<VoiceRejoiner> rejoiner_);

    // From the bot's periodic timer.
    void tick();

private:
    bool leaveIfAlone(const PlaybackController::IdleInfo &info);
    void leaveIfIdle(const PlaybackController::IdleInfo &info);
    void leaveVoice(uint64_t guildId, uint64_t textChannelId, const char *farewell);

    static constexpr int64_t IDLE_TIMEOUT_SECONDS = 5 * 60;
    static constexpr int64_t ALONE_TIMEOUT_SECONDS = 5 * 60;

    dpp::cluster &bot;
    std::shared_ptr<PlaybackController> playback;
    std::shared_ptr<VoiceRejoiner> rejoiner;
    bool stayWhenAlone{false};
    EmptyRoomClock emptyRoomClock{ALONE_TIMEOUT_SECONDS};
};
