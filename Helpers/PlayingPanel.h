#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace dpp {
class cluster;
struct confirmation_callback_t;
}

// The "Playing:" message of one song: its buttons go when the song does.
// =======================================================
// Rules:
// - retire() edits RETIRE_AFTER_SECONDS later, so that a click answered
//   around the song's end, which sends the row back, lands first. A click
//   answered after the edit brings the row back for good; it still
//   controls whatever plays.
// - retire() finds no message id when dpp's answer to the announcement is
//   later than the edit; the row stays then.
// - retire() sends the title again: dpp's message_edit always sends a text,
//   so the status line of the last click goes with the buttons.
// =======================================================
class PlayingPanel
{
public:
    PlayingPanel(dpp::cluster &bot_, uint64_t channelId_, std::string title_);

    // dpp's answer to the announcement; called on a dpp thread.
    void announced(const dpp::confirmation_callback_t &answer);

    // The song is over. Does nothing for a song that never announced.
    static void retire(const std::shared_ptr<PlayingPanel> &panel);

private:
    dpp::cluster &bot;
    const uint64_t channelId;
    const std::string title;
    std::atomic<uint64_t> messageId{0};

    static constexpr uint64_t RETIRE_AFTER_SECONDS = 3;
};
