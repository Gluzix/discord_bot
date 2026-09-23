#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace dpp {
struct button_click_t;
}

class ICommand;
class PlaybackController;

// Takes every click on a "Playing:" button to the command behind it.
// =======================================================
// Rules:
// - handle() does no playback work: the command behind the button does, in
//   its execute(), audience check included. It only asks
//   playback->isPaused(), to turn play/pause into pause or resume.
// =======================================================
class ButtonRouter
{
public:
    ButtonRouter(const std::unordered_map<std::string, std::unique_ptr<ICommand>> &commands_,
                 std::shared_ptr<PlaybackController> playback_);
    void handle(const dpp::button_click_t &event);

private:
    const std::unordered_map<std::string, std::unique_ptr<ICommand>> &commands;
    std::shared_ptr<PlaybackController> playback;
};
