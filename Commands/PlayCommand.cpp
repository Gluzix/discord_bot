#include "PlayCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>
#include <cctype>

PlayCommand::PlayCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("play", "plays from a youtube link or searches by title")
    , playback(playback_)
{
}

// User input ends up on a yt-dlp command line; allow only plain YouTube
// links so nothing can break out of the quotes or inject extra arguments.
static bool isAllowedYoutubeUrl(const std::string &url)
{
    static const char* allowedPrefixes[] = {
        "https://www.youtube.com/",
        "https://youtube.com/",
        "https://m.youtube.com/",
        "https://music.youtube.com/",
        "https://youtu.be/",
    };

    if (url.empty() || url.size() > 250) {
        return false;
    }

    bool prefixOk = false;
    for (const char* prefix : allowedPrefixes) {
        if (url.rfind(prefix, 0) == 0) {
            prefixOk = true;
            break;
        }
    }
    if (!prefixOk) {
        return false;
    }

    const std::string allowedSpecialChars = "-_.~:/?=&%+@";
    for (char c : url) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && allowedSpecialChars.find(c) == std::string::npos) {
            return false;
        }
    }
    return true;
}

// Free-text search lands on the same yt-dlp command line - keep it short and
// free of quotes/control characters so it can't escape the quoted argument.
static bool isReasonableSearchQuery(const std::string &query)
{
    if (query.empty() || query.size() > 150) {
        return false;
    }
    for (char c : query) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || c == '"' || c == '\\') {
            return false;
        }
    }
    return true;
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
    // search for the first matching video.
    std::string target;
    if (isAllowedYoutubeUrl(input)) {
        target = input;
    } else if (isReasonableSearchQuery(input)) {
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
