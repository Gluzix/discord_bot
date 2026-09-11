#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class JoinCommand : public Command
{
public:
    explicit JoinCommand(std::shared_ptr<PlaybackController> playback_);

    void execute(const dpp::slashcommand_t& event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
