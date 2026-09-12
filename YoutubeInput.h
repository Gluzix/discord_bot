#pragma once

#include <cctype>
#include <string>

// User input ends up on a yt-dlp command line. These checks keep it to plain
// YouTube links and tame search text, so nothing can break out of the quoted
// argument or inject extra arguments.
namespace youtube {

inline bool isAllowedUrl(const std::string &url)
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

// Free text stays short and free of quotes/control characters.
inline bool isReasonableSearchQuery(const std::string &query)
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

// Any allowed link that names a playlist - a watch?v=...&list=... link too.
inline bool isPlaylistUrl(const std::string &url)
{
    return isAllowedUrl(url) && url.find("list=") != std::string::npos;
}

// A playlist page with no video of its own; /play has nothing to play there.
inline bool isPlaylistPageUrl(const std::string &url)
{
    return isPlaylistUrl(url) && url.find("/playlist?") != std::string::npos;
}

}
