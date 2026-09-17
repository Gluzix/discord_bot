#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class SeekCommand : public Command
{
public:
    explicit SeekCommand(std::shared_ptr<PlaybackController> playback_);

    dpp::slashcommand definition(dpp::snowflake botId) const override;
    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
