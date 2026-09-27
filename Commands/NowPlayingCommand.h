#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class NowPlayingCommand : public Command
{
public:
    explicit NowPlayingCommand(std::shared_ptr<PlaybackController> playback_);

    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
