#include "QueueText.h"
#include "Messages.h"

namespace {

const size_t MAX_LISTED = 15;
const size_t MAX_LENGTH = 2000;

size_t utf16Length(const std::string &text)
{
    size_t length = 0;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c & 0xC0) != 0x80) {
            length += c >= 0xF0 ? 2 : 1;
        }
    }
    return length;
}

std::string moreLine(size_t count)
{
    return messages::queueMorePrefix + std::to_string(count) + messages::queueMoreSuffix + "\n";
}

}

namespace queuetext {

std::string render(const PlaybackController::QueueSnapshot &snapshot)
{
    std::string text;
    if (!snapshot.current.empty()) {
        text += messages::queueNowPlayingPrefix + ("**" + snapshot.current + "**");
        if (snapshot.loop == PlaybackController::LoopMode::Song) {
            text += messages::queueLoopSongSuffix;
        } else if (snapshot.loop == PlaybackController::LoopMode::Queue) {
            text += messages::queueLoopQueueSuffix;
        }
        text += "\n";
    }

    size_t length = utf16Length(text);
    size_t listed = 0;
    while (listed < snapshot.queued.size() && listed < MAX_LISTED) {
        const std::string line = std::to_string(listed + 1) + ". " + snapshot.queued[listed] + "\n";
        const size_t left = snapshot.queued.size() - (listed + 1);
        const size_t lineLength = utf16Length(line);
        const size_t moreLength = left > 0 ? utf16Length(moreLine(left)) : 0;
        if (length + lineLength + moreLength > MAX_LENGTH) {
            break;
        }
        text += line;
        length += lineLength;
        ++listed;
    }
    if (listed < snapshot.queued.size()) {
        text += moreLine(snapshot.queued.size() - listed);
    }
    return text;
}

}
