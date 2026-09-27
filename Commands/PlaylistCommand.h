#pragma once

#include "Command.h"

#include <memory>

class IMediaResolver;
class PlaybackController;

class PlaylistCommand : public Command
{
public:
    PlaylistCommand(std::shared_ptr<PlaybackController> playback_,
                    std::shared_ptr<IMediaResolver> resolver_);

    dpp::slashcommand definition(dpp::snowflake botId) const override;
    void execute(const dpp::slashcommand_t &event) override;

private:
    std::shared_ptr<PlaybackController> playback;
    std::shared_ptr<IMediaResolver> resolver;
};
