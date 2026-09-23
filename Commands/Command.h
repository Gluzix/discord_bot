#pragma once

#include "ICommand.h"
#include "Messages.h"

#include <string>

namespace dpp {
struct interaction_create_t;
}

class PlaybackController;

class Command : public ICommand
{
public:
    Command(const Command& other) = delete;
    Command(Command&& other) = delete;
    Command() = delete;
    Command& operator=(const Command& other) = delete;

    Command(std::string name_, std::string description_);
    ~Command() override;

    std::string name() const override;
    std::string description() const override;

    // Generic behavior: reply with the description text.
    void execute(const dpp::slashcommand_t &event) override;
    // Generic behavior: a command without buttons knows no click.
    void execute(const dpp::button_click_t &event, const std::string &argument) override;

protected:
    // A click answers only the clicker; a slash command answers the channel.
    static void reply(const dpp::interaction_create_t &event, const std::string &text);

    // The audience-loyalty rule shared by every playback-control command:
    // the user must be in the bot's channel (or the bot unconnected/alone).
    // Replies with the refusal and returns false when control is denied.
    bool userMayControl(const dpp::interaction_create_t &event, const char *refusalReply = messages::mustBeWithBot);

    // The summoning rule for /play and /join: while a session is active
    // (playing or queued), only its audience may call the bot elsewhere;
    // an idle bot follows anyone. Replies with the busy-channel refusal
    // (with a clickable channel mention) and returns false when denied.
    bool userMaySummon(const dpp::slashcommand_t &event, PlaybackController &playback);

    std::string cmdName{};
    std::string cmdDescription{};
};
