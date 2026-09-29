#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class ShuffleCommand : public Command
{
public:
    explicit ShuffleCommand(std::shared_ptr<PlaybackController> playback_);

    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
