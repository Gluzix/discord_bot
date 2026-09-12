#include "PlayCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "YoutubeInput.h"
#include "Messages.h"

#include <dpp/dpp.h>

PlayCommand::PlayCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("play", "plays from a youtube link or searches by title")
    , playback(playback_)
{
}

dpp::slashcommand PlayCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(dpp::command_option(dpp::co_string, "song", "YouTube link or a song title to search for", true));
    return cmd;
}

void PlayCommand::execute(const dpp::slashcommand_t &event)
{
    if (!userMaySummon(event, *playback)) {
        return;
    }

    if (VoiceConnector::ensureJoined(event, playback.get()) == VoiceConnector::Result::UserNotInVoice) {
        event.reply(messages::userNotInVoice);
        return;
    }

    // The interaction's one response slot: ack with a placeholder that the
    // pipeline later edits into "Playing: <title>" or an error message.
    event.reply(messages::lookingForSong);

    std::string input;
    auto songParameter = event.get_parameter("song");
    if (std::holds_alternative<std::string>(songParameter)) {
        input = std::get<std::string>(songParameter);
    }

    // A valid YouTube link plays directly; anything else becomes a yt-dlp
    // search for the first matching video. A bare playlist page has no
    // video to play - point at /playlist instead.
    std::string target;
    if (youtube::isPlaylistPageUrl(input)) {
        event.edit_original_response(dpp::message(messages::usePlaylistCommand));
        return;
    }
    if (youtube::isAllowedUrl(input)) {
        target = input;
    } else if (youtube::isReasonableSearchQuery(input)) {
        target = "ytsearch1:" + input;
    } else {
        event.edit_original_response(dpp::message(messages::invalidSongInput));
        return;
    }

    size_t waitingPosition = playback->play(target, event);
    if (waitingPosition > 0) {
        // Something is already playing; the placeholder becomes the queue
        // confirmation and morphs into "Playing: <title>" when its turn comes.
        event.edit_original_response(dpp::message(messages::queuedAtPrefix + std::to_string(waitingPosition)));
    }
}
