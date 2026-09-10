#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class StopCommand : public Command
{
public:
    explicit StopCommand(std::shared_ptr<PlaybackController> playback_);

    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
