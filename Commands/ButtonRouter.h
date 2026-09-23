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
// - The router does no playback work: the command behind the button does,
//   audience check included. playback is only asked whether a song is
//   paused, to turn play/pause into pause or resume.
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
