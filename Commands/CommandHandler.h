#pragma once

#include <dpp/dpp.h>
#include "ICommand.h"

#include <memory>
#include <unordered_map>
#include <utility>

class PlaybackController;
class VoiceRejoiner;

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
};
