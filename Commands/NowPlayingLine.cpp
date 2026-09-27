#include "NowPlayingLine.h"
#include "TimeText.h"
#include "Messages.h"

namespace nowplaying {

std::string line(const PlaybackController::NowPlaying &now)
{
    if (!now.playing) {
        return messages::nowPlayingNothing;
    }

    std::string text = messages::queueNowPlayingPrefix + ("**" + now.label + "**");
    if (now.positionKnown) {
        text += " - " + timetext::formatProgress(now.positionSeconds, now.durationSeconds);
    }
    if (now.paused) {
        text += messages::nowPlayingPausedSuffix;
    }
    if (now.loop == PlaybackController::LoopMode::Song) {
        text += messages::queueLoopSongSuffix;
    } else if (now.loop == PlaybackController::LoopMode::Queue) {
        text += messages::queueLoopQueueSuffix;
    }
    return text;
}

bool isReply(const std::string &content)
{
    return content.rfind(messages::queueNowPlayingPrefix, 0) == 0;
}

}
