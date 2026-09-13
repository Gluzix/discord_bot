#pragma once

#include <string>

class LabelCreator
{
public:
    LabelCreator();

    static std::string displayLabelFor(const std::string &target);

    // A resolved song renders as a masked link - the title as clickable text,
    // <> suppressing the embed preview. Unresolved songs fall back to the target.
    static std::string renderLabel(const std::string &title, const std::string &webpageUrl, const std::string &fallbackTarget);

};
