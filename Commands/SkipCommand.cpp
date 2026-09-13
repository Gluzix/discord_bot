#include "SkipCommand.h"
#include "PlaybackController.h"
#include "Messages.h"

#include <dpp/dpp.h>

#include <algorithm>

SkipCommand::SkipCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("skip", "skips the current song, or several")
    , playback(playback_)
{
}

dpp::slashcommand SkipCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(
        dpp::command_option(dpp::co_integer, "count", "how many songs to skip, the current one included (default 1)", false)
            .set_min_value(1)
            .set_max_value(100)
    );
    return cmd;
}

void SkipCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    int64_t count = 1;
    auto countParameter = event.get_parameter("count");
    if (std::holds_alternative<int64_t>(countParameter)) {
        count = std::clamp<int64_t>(std::get<int64_t>(countParameter), 1, 100);
    }

    size_t skipped = playback->skip(static_cast<size_t>(count));
    if (skipped == 0) {
        event.reply(messages::nothingPlaying);
    } else if (skipped == 1) {
        event.reply(messages::skipped);
    } else {
        event.reply(messages::skippedManyPrefix + std::to_string(skipped) + messages::skippedManySuffix);
    }
}
