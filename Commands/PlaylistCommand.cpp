#include "PlaylistCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "WindowsProcessRunner.h"
#include "YoutubeInput.h"
#include "Messages.h"

#include <dpp/dpp.h>

PlaylistCommand::PlaylistCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("playlist", "queues every video of a youtube playlist")
    , playback(playback_)
{
}

dpp::slashcommand PlaylistCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(dpp::command_option(dpp::co_string, "link", "YouTube playlist link", true));
    return cmd;
}

void PlaylistCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMaySummon(event, *playback)) {
        return;
    }

    std::string link;
    auto linkParameter = event.get_parameter("link");
    if (std::holds_alternative<std::string>(linkParameter)) {
        link = std::get<std::string>(linkParameter);
    }
    if (!youtube::isPlaylistUrl(link)) {
        event.reply(messages::invalidPlaylistInput);
        return;
    }

    if (VoiceConnector::ensureJoined(event, playback.get()) == VoiceConnector::Result::UserNotInVoice) {
        event.reply(messages::userNotInVoice);
        return;
    }

    // Listing takes a few seconds: the placeholder keeps Discord's 3-second
    // interaction deadline happy and later becomes the summary.
    event.reply(messages::readingPlaylist);

    const size_t MAX_ENTRIES = 100;
    PlaylistListing listing = WindowsProcessRunner::listPlaylist(link, MAX_ENTRIES);
    if (listing.entries.empty()) {
        event.edit_original_response(dpp::message(messages::playlistEmpty));
        return;
    }

    size_t queued = playback->playPlaylist(listing.entries, event);

    // "Queued 37 songs from [Title](<link>)" - the title is untrusted page
    // text, so no mentions; <> keeps the embed preview away.
    std::string name = listing.title.empty() ? std::string(messages::playlistFallbackName) : listing.title;
    std::string text = messages::playlistQueuedPrefix + std::to_string(queued)
        + (queued == 1 ? messages::playlistQueuedOne : messages::playlistQueuedMany)
        + "[" + name + "](<" + link + ">)";
    if (listing.totalCount > queued) {
        text += messages::playlistTruncatedPrefix + std::to_string(listing.totalCount) + messages::playlistTruncatedSuffix;
    }
    dpp::message summary(text);
    summary.set_allowed_mentions();
    event.edit_original_response(summary);
}
