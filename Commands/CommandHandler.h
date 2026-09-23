#pragma once

#include <dpp/dpp.h>
#include "ICommand.h"
#include "ButtonRouter.h"

#include <memory>
#include <unordered_map>
#include <utility>

class PlaybackController;
class VoiceRejoiner;

// Wires the commands to the bot, registers them and owns the idle timer.
// =======================================================
// Rules:
// - The rejoiner is built before the commands: LeaveCommand takes it.
// - dpp's shard registry is the only truth about which voice client is live
//   and whether the bot is in voice: both are asked of it, never tracked
//   here.
// - While a rejoin is pending, the bot's own leave goes to the rejoiner:
//   playback must not see it, or it would stop the song and wipe the queue.
// - The idle timer stops playback before it disconnects: our threads let go
//   of the voice client first.
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

    std::shared_ptr<dpp::cluster> bot;
    std::shared_ptr<PlaybackController> playback;
    std::shared_ptr<VoiceRejoiner> rejoiner;
    std::unordered_map<std::string, std::unique_ptr<ICommand>> commands{};
    std::unique_ptr<ButtonRouter> buttonRouter;
};
