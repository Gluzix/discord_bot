#include "QueueCommand.h"
#include "QueueText.h"
#include "PlaybackController.h"
#include "Messages.h"

#include <dpp/dpp.h>

QueueCommand::QueueCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("queue", "shows the current song queue")
    , playback(playback_)
{
}

void QueueCommand::execute(const dpp::slashcommand_t &event)
{
    PlaybackController::QueueSnapshot snapshot = playback->queueSnapshot();

    if (snapshot.current.empty() && snapshot.queued.empty()) {
        event.reply(messages::queueEmpty);
        return;
    }

    // Titles and urls are untrusted input - never let them ping anyone.
    dpp::message reply(queuetext::render(snapshot));
    reply.set_allowed_mentions();
    event.reply(reply);
}
