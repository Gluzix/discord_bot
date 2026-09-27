#include "NowPlayingCommand.h"
#include "NowPlayingLine.h"
#include "PlaybackController.h"
#include "PlaybackButtons.h"
#include "Messages.h"

#include <dpp/dpp.h>

NowPlayingCommand::NowPlayingCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("nowplaying", "what is playing and where it is")
    , playback(playback_)
{
}

void NowPlayingCommand::execute(const dpp::slashcommand_t &event)
{
    PlaybackController::NowPlaying now = playback->nowPlaying();

    if (!now.playing) {
        event.reply(messages::nothingPlaying);
        return;
    }

    // The label is untrusted input - never let it ping anyone.
    dpp::message msg(nowplaying::line(now));
    msg.set_allowed_mentions();
    msg.add_component(buttons::controlRow());
    event.reply(msg);
}
