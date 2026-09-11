#pragma once

#include <string>

struct ResolvedMedia
{
    std::string title;
    std::string webpageUrl; // canonical YouTube page (also for search results)
    std::string directUrl;  // empty on failure
};

class WindowsProcessRunner
{
public:
    // Runs yt-dlp once to resolve both the video title and the direct media
    // URL. The target is either a YouTube url or a "ytsearch1:<query>"
    // search expression. directUrl is empty on failure.
    static ResolvedMedia resolveMedia(const std::string &target);
};
