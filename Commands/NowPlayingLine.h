#pragma once

#include "PlaybackController.h"

#include <string>

// The first line of a /nowplaying reply, written and recognised here.
namespace nowplaying {

// What /nowplaying says about the song; without a song, the prefix and
// "nothing", so the message is still known as a /nowplaying reply.
std::string line(const PlaybackController::NowPlaying &now);

// True for the text of a message that /nowplaying sent.
bool isReply(const std::string &content);

}
