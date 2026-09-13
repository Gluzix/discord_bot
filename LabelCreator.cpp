#include "LabelCreator.h"
#include "Messages.h"

LabelCreator::LabelCreator() {}

// Queue entries hold the raw yt-dlp target; show searches in a friendlier
// form until the real title is resolved. <> around a bare url stops Discord
// from unfurling an embed preview for it.
std::string LabelCreator::displayLabelFor(const std::string &target)
{
    const std::string searchPrefix = "ytsearch1:";
    if (target.rfind(searchPrefix, 0) == 0) {
        return messages::searchLabelPrefix + target.substr(searchPrefix.size());
    }
    if (target.rfind("http", 0) == 0) {
        return "<" + target + ">";
    }
    return target;
}

// A resolved song renders as a masked link - the title as clickable text,
// <> suppressing the embed preview. Unresolved songs fall back to the target.
std::string LabelCreator::renderLabel(const std::string &title, const std::string &webpageUrl, const std::string &fallbackTarget)
{
    if (!title.empty() && !webpageUrl.empty()) {
        return "[" + title + "](<" + webpageUrl + ">)";
    }
    if (!title.empty()) {
        return title;
    }
    return displayLabelFor(fallbackTarget);
}
