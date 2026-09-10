#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class QueueCommand : public Command
{
public:
    explicit QueueCommand(std::shared_ptr<PlaybackController> playback_);

    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
