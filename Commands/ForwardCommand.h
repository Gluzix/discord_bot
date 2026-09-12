#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class ForwardCommand : public Command
{
public:
    explicit ForwardCommand(std::shared_ptr<PlaybackController> playback_);

    dpp::slashcommand definition(dpp::snowflake botId) const override;
    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
};
