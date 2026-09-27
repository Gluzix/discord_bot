#pragma once

#include <dpp/dpp.h>
#include "ICommand.h"
#include "ButtonRouter.h"

#include <memory>
#include <unordered_map>
#include <utility>

class IMediaResolver;
class PlaybackController;
class VoiceRejoiner;

// Wires the commands to the bot, registers them and starts the leave timer.
// =======================================================
// Rules:
// - prepare() builds the rejoiner before the commands: LeaveCommand takes it.
// - dpp's shard registry is the only truth about which voice client is live:
//   it is asked in setupBot()'s voice-client lookup, never tracked here.
// - While a rejoin is pending, setupBot()'s voice-state handler hands the
//   bot's own leave to rejoiner->onBotLeft(): playback must not see it, or it
//   would stop the song and wipe the queue.
// - Every handler in setupBot() captures shared_ptrs, never this: dpp runs
//   them on its pool threads, which nothing joins before this object dies.
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
        commands->emplace(cmd->name(), std::move(cmd));
    }

    void setupCommands();
    void setupBot();

    std::shared_ptr<dpp::cluster> bot;
    std::shared_ptr<IMediaResolver> resolver;
    std::shared_ptr<PlaybackController> playback;
    std::shared_ptr<VoiceRejoiner> rejoiner;
    std::shared_ptr<CommandMap> commands{std::make_shared<CommandMap>()};
    std::shared_ptr<ButtonRouter> buttonRouter;
};
