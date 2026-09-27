#include "NowPlayingLine.h"
#include "Check.h"

#include <string>

using LoopMode = PlaybackController::LoopMode;

// 1:30 into a song of 3:45, nothing else set.
static PlaybackController::NowPlaying playing()
{
    PlaybackController::NowPlaying now;
    now.playing = true;
    now.label = "[Song](<https://www.youtube.com/watch?v=x>)";
    now.positionKnown = true;
    now.positionSeconds = 90;
    now.durationSeconds = 225;
    return now;
}

int main()
{
    const std::string song = "Now playing: **[Song](<https://www.youtube.com/watch?v=x>)**";

    { // a song with a known position
        const std::string line = nowplaying::line(playing());
        check(line == song + " - 1:30 / 3:45", "known position: the song, then where it is");
        check(nowplaying::isReply(line), "known position: isReply");
    }

    { // a song still opening
        PlaybackController::NowPlaying now = playing();
        now.positionKnown = false;
        const std::string line = nowplaying::line(now);
        check(line == song, "unknown position: the song alone");
        check(nowplaying::isReply(line), "unknown position: isReply");
    }

    { // paused
        PlaybackController::NowPlaying now = playing();
        now.paused = true;
        const std::string line = nowplaying::line(now);
        check(line == song + " - 1:30 / 3:45 (paused)", "paused: (paused) after the position");
        check(nowplaying::isReply(line), "paused: isReply");
    }

    { // looping the song
        PlaybackController::NowPlaying now = playing();
        now.loop = LoopMode::Song;
        const std::string line = nowplaying::line(now);
        check(line == song + " - 1:30 / 3:45 (loop: song)", "loop song: the suffix /queue shows");
        check(nowplaying::isReply(line), "loop song: isReply");
    }

    { // looping the queue
        PlaybackController::NowPlaying now = playing();
        now.loop = LoopMode::Queue;
        const std::string line = nowplaying::line(now);
        check(line == song + " - 1:30 / 3:45 (loop: queue)", "loop queue: the suffix /queue shows");
        check(nowplaying::isReply(line), "loop queue: isReply");
    }

    { // nothing playing
        const std::string line = nowplaying::line(PlaybackController::NowPlaying{});
        check(line == "Now playing: nothing", "nothing playing: the prefix and nothing");
        check(nowplaying::isReply(line), "nothing playing: isReply");
    }

    check(!nowplaying::isReply("Playing: [Song](<https://www.youtube.com/watch?v=x>)"),
          "a \"Playing:\" title is no /nowplaying reply");
    check(!nowplaying::isReply(""), "an empty text is no /nowplaying reply");

    return summary();
}
