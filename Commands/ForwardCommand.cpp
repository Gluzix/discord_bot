#include "ForwardCommand.h"
#include "PlaybackController.h"
#include "TimeText.h"

#include <dpp/dpp.h>

#include <algorithm>

ForwardCommand::ForwardCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("forward", "jump ahead in the current song")
    , playback(playback_)
{
}

dpp::slashcommand ForwardCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(
        dpp::command_option(dpp::co_integer, "seconds", "how far to jump (default 10)", false)
            .set_min_value(1)
            .set_max_value(600)
    );
    return cmd;
}

void ForwardCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    int64_t seconds = 10;
    auto secondsParameter = event.get_parameter("seconds");
    if (std::holds_alternative<int64_t>(secondsParameter)) {
        seconds = std::clamp<int64_t>(std::get<int64_t>(secondsParameter), 1, 600);
    }

    PlaybackController::SeekResult result = playback->seekBy(static_cast<int>(seconds));
    if (!result.playing) {
        event.reply(messages::nothingPlaying);
    } else if (!result.seekable) {
        event.reply(messages::songStillLoading);
    } else {
        event.reply(messages::forwardedTo
                    + timetext::formatProgress(result.positionSeconds, result.durationSeconds));
    }
}
