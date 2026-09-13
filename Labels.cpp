#include "Labels.h"
#include "Messages.h"

namespace labels {

std::string displayFor(const std::string &target)
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

std::string render(const std::string &title, const std::string &webpageUrl, const std::string &fallbackTarget)
{
    if (!title.empty() && !webpageUrl.empty()) {
        return "[" + title + "](<" + webpageUrl + ">)";
    }
    if (!title.empty()) {
        return title;
    }
    return displayFor(fallbackTarget);
}

}
