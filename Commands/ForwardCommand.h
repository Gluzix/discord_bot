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
    void execute(const dpp::button_click_t &event, const std::string &argument) override;

private:
    void run(const dpp::interaction_create_t &event, int seconds);

    std::shared_ptr<PlaybackController> playback;
};
