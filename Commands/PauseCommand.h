#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class PauseCommand : public Command
{
public:
    PauseCommand(std::shared_ptr<PlaybackController> playback_);
    void execute(const dpp::slashcommand_t &event);

private:
    std::shared_ptr<PlaybackController> playback;
};
