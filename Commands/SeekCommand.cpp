#include "SeekCommand.h"
#include "PlaybackController.h"
#include "TimeText.h"

#include <dpp/dpp.h>

SeekCommand::SeekCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("seek", "jump to a position in the current song")
    , playback(playback_)
{
}

dpp::slashcommand SeekCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(
        dpp::command_option(dpp::co_string, "position", "where to jump: 90, 1:30 or 1:02:03", true)
    );
    return cmd;
}

void SeekCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    std::string position;
    auto positionParameter = event.get_parameter("position");
    if (std::holds_alternative<std::string>(positionParameter)) {
        position = std::get<std::string>(positionParameter);
    }

    std::optional<int> seconds = timetext::parsePosition(position);
    if (!seconds) {
        event.reply(messages::invalidPosition);
        return;
    }

    PlaybackController::SeekResult result = playback->seekTo(*seconds);
    if (!result.playing) {
        event.reply(messages::nothingPlaying);
    } else if (!result.seekable) {
        event.reply(messages::songStillLoading);
    } else {
        event.reply(messages::jumpedTo
                    + timetext::formatProgress(result.positionSeconds, result.durationSeconds));
    }
}
