#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class ResumeCommand : public Command
{
public:
    ResumeCommand(std::shared_ptr<PlaybackController> playback_);
    void execute(const dpp::slashcommand_t &event);

private:
    std::shared_ptr<PlaybackController> playback;
};
