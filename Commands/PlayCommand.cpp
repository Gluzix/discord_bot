#include "PlayCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"

#include <dpp/dpp.h>
#include <cctype>

PlayCommand::PlayCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("play", "plays audio from the given youtube link")
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

dpp::slashcommand PlayCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(dpp::command_option(dpp::co_string, "url", "YouTube video link", true));
    return cmd;
}

void PlayCommand::execute(const dpp::slashcommand_t &event)
{
    if (VoiceConnector::ensureJoined(event) == VoiceConnector::Result::UserNotInVoice) {
        event.reply("You don't seem to be in a voice channel!");
        return;
    }

    // The interaction's one response slot: ack with a placeholder that the
    // pipeline later edits into "Playing: <title>" or an error message.
    event.reply("Looking for your song...");

    std::string yturl;
    auto urlParameter = event.get_parameter("url");
    if (std::holds_alternative<std::string>(urlParameter)) {
        yturl = std::get<std::string>(urlParameter);
    }

    if (!isAllowedYoutubeUrl(yturl)) {
        event.edit_original_response(dpp::message("That doesn't look like a YouTube link I can play!"));
        return;
    }

    size_t waitingPosition = playback->play(yturl, event);
    if (waitingPosition > 0) {
        // Something is already playing; the placeholder becomes the queue
        // confirmation and morphs into "Playing: <title>" when its turn comes.
        event.edit_original_response(dpp::message("Queued at position " + std::to_string(waitingPosition)));
    }
}
