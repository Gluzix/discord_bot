#pragma once

#include "ICommand.h"

#include <memory>

namespace dpp {
struct button_click_t;
}

class PlaybackController;

// Takes every click on a "Playing:" button to the command behind it.
// =======================================================
// Rules:
// - handle() does no playback work: the command behind the button does, in
//   its execute(), audience check included. It only asks
//   playback->isPaused(), to turn play/pause into pause or resume.
// - Held by a shared_ptr made in CommandHandler::prepare():
//   setupBot()'s click handler runs on a dpp pool thread.
// =======================================================
class ButtonRouter
{
public:
    ButtonRouter(std::shared_ptr<const CommandMap> commands_,
                 std::shared_ptr<PlaybackController> playback_);
    void handle(const dpp::button_click_t &event);

private:
    std::shared_ptr<const CommandMap> commands;
    std::shared_ptr<PlaybackController> playback;
};
