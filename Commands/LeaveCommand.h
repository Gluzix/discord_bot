#pragma once

#include "Command.h"

#include <memory>

class PlaybackController;
class VoiceRejoiner;

// Sends the bot out of its voice channel.
// =======================================================
// Rules:
// - In execute(), playback->stop() comes before the disconnect: it waits
//   (bounded) for the playback threads to join, so nothing of ours should
//   still touch the voice client when the disconnect destroys it.
// - The pending rejoin is cancelled before the disconnect too: the state
//   update it causes must not be taken for a rejoin's own leave, or the bot
//   would come straight back (rejoiner->cancel() in execute()).
// =======================================================
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
