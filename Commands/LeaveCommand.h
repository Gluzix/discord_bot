#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;
class VoiceRejoiner;

class LeaveCommand : public Command
{
public:
    LeaveCommand(std::shared_ptr<PlaybackController> playback_,
                 std::shared_ptr<VoiceRejoiner> rejoiner_);

    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
    std::shared_ptr<VoiceRejoiner> rejoiner; // null when there is no bot
};
