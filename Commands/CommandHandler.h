#pragma once

#include <dpp/dpp.h>
#include "ICommand.h"
#include "ButtonRouter.h"
#include "PlaybackController.h"
#include "EmptyRoomClock.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

class VoiceRejoiner;

// Wires the commands to the bot, registers them and owns both leave timers.
// =======================================================
// Rules:
// - prepare() builds the rejoiner before the commands: LeaveCommand takes it.
// - dpp's shard registry is the only truth about which voice client is live
//   and whether the bot is in voice: both are asked of it, in setupBot()'s
//   voice-client lookup, leaveIfIdle() and VoiceConnector::botChannel(),
//   never tracked here.
// - While a rejoin is pending, setupBot()'s voice-state handler hands the
//   bot's own leave to rejoiner->onBotLeft(): playback must not see it, or it
//   would stop the song and wipe the queue.
// - leaveVoice() calls playback->stop() before it disconnects, so our threads
//   let go of the voice client first, and cancels the pending rejoin, or the
//   rejoiner would bring the bot straight back.
// - emptyRoomClock has no lock because only the timer touches it
//   (leaveIfAlone()), and dpp runs every tick of a timer on its one
//   event-loop thread.
// =======================================================
class CommandHandler
{
public:
    CommandHandler();
    void setBot(std::shared_ptr<dpp::cluster> bot_);
    void prepare();

private:
    template<typename T, typename... Args>
    void add(Args&&... args)
    {
        auto cmd = std::make_unique<T>(std::forward<Args>(args)...);
        commands.emplace(cmd->name(), std::move(cmd));
    }

    void setupCommands();
    void setupBot();
    bool leaveIfAlone(const PlaybackController::IdleInfo &info);
    void leaveIfIdle(const PlaybackController::IdleInfo &info);
    void leaveVoice(uint64_t guildId, uint64_t textChannelId, const char *farewell);

    std::shared_ptr<dpp::cluster> bot;
    std::shared_ptr<PlaybackController> playback;
    std::shared_ptr<VoiceRejoiner> rejoiner;
    std::unordered_map<std::string, std::unique_ptr<ICommand>> commands{};
    std::unique_ptr<ButtonRouter> buttonRouter;

    static constexpr int64_t IDLE_TIMEOUT_SECONDS = 5 * 60;
    static constexpr int64_t ALONE_TIMEOUT_SECONDS = 5 * 60;
    EmptyRoomClock emptyRoomClock{ALONE_TIMEOUT_SECONDS};
    bool stayWhenAlone{false};
};
