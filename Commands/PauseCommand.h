#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;

class PauseCommand : public Command
{
public:
    PauseCommand(std::shared_ptr<PlaybackController> playback_);
    void execute(const dpp::slashcommand_t &event) override;
    void execute(const dpp::button_click_t &event, const std::string &argument) override;

private:
    void run(const dpp::interaction_create_t &event);

    std::shared_ptr<PlaybackController> playback;
};
