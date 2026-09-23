#include "RewindCommand.h"
#include "PlaybackController.h"
#include "TimeText.h"

#include <dpp/dpp.h>

#include <algorithm>
#include <charconv>

RewindCommand::RewindCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("rewind", "jump back in the current song")
    , playback(playback_)
{
}

dpp::slashcommand RewindCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(
        dpp::command_option(dpp::co_integer, "seconds", "how far to jump back (default 10)", false)
            .set_min_value(1)
            .set_max_value(600)
    );
    return cmd;
}

void RewindCommand::execute(const dpp::slashcommand_t &event)
{
    int64_t seconds = 10;
    auto secondsParameter = event.get_parameter("seconds");
    if (std::holds_alternative<int64_t>(secondsParameter)) {
        seconds = std::clamp<int64_t>(std::get<int64_t>(secondsParameter), 1, 600);
    }

    run(event, static_cast<int>(seconds));
}

void RewindCommand::execute(const dpp::button_click_t &event, const std::string &argument)
{
    int seconds = 10;
    // Stays at the default when the argument isn't a number.
    std::from_chars(argument.data(), argument.data() + argument.size(), seconds);
    run(event, std::clamp(seconds, 1, 600));
}

void RewindCommand::run(const dpp::interaction_create_t &event, int seconds)
{
    if (!userMayControl(event)) {
        return;
    }

    PlaybackController::SeekResult result = playback->seekBy(-seconds);
    if (!result.playing) {
        reply(event, messages::nothingPlaying);
    } else if (!result.seekable) {
        reply(event, messages::songStillLoading);
    } else {
        reply(event, messages::rewoundTo
                     + timetext::formatProgress(result.positionSeconds, result.durationSeconds));
    }
}
