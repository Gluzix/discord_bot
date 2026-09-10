#include "QueueCommand.h"
#include "PlaybackController.h"

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
        event.reply("The queue is empty and nothing is playing.");
        return;
    }

    std::string text;
    if (!snapshot.current.empty()) {
        text += "Now playing: **" + snapshot.current + "**\n";
    }

    const size_t MAX_LISTED = 15; // stay far below Discord's 2000 char limit
    for (size_t i = 0; i < snapshot.queued.size() && i < MAX_LISTED; ++i) {
        text += std::to_string(i + 1) + ". " + snapshot.queued[i] + "\n";
    }
    if (snapshot.queued.size() > MAX_LISTED) {
        text += "...and " + std::to_string(snapshot.queued.size() - MAX_LISTED) + " more\n";
    }

    // Titles and urls are untrusted input - never let them ping anyone.
    dpp::message reply(text);
    reply.set_allowed_mentions();
    event.reply(reply);
}
