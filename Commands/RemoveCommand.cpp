#include "RemoveCommand.h"
#include "PlaybackController.h"

#include <dpp/dpp.h>

RemoveCommand::RemoveCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("remove", "take a song out of the queue")
    , playback(playback_)
{
}

dpp::slashcommand RemoveCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(
        dpp::command_option(dpp::co_integer, "position", "its number in /queue", true)
            .set_min_value(1)
    );
    return cmd;
}

void RemoveCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMayControl(event)) {
        return;
    }

    int64_t position = 0;
    auto positionParameter = event.get_parameter("position");
    if (std::holds_alternative<int64_t>(positionParameter)) {
        position = std::get<int64_t>(positionParameter);
    }

    // Not clamped: a clamp would turn a bad value into a real position.
    const PlaybackController::RemoveResult result = playback->remove(static_cast<size_t>(position));
    switch (result.outcome) {
    case queueedit::RemoveOutcome::Removed: {
        // The label is untrusted input - never let it ping anyone.
        dpp::message reply(messages::removedPrefix + result.label + messages::removedSuffix);
        reply.set_allowed_mentions();
        event.reply(reply);
        break;
    }
    case queueedit::RemoveOutcome::NoSuchPosition:
        event.reply(messages::noSuchPosition);
        break;
    case queueedit::RemoveOutcome::Held:
        event.reply(messages::removeHeld);
        break;
    case queueedit::RemoveOutcome::UpNext:
        event.reply(messages::removeUpNext);
        break;
    }
}
