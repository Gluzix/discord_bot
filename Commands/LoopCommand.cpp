#include "LoopCommand.h"
#include "PlaybackController.h"
#include "VoiceConnector.h"
#include "Messages.h"

#include <dpp/dpp.h>

LoopCommand::LoopCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("loop", "loop the current song, the whole queue, or turn looping off")
    , playback(playback_)
{
}

dpp::slashcommand LoopCommand::definition(dpp::snowflake botId) const
{
    dpp::slashcommand cmd(name(), description(), botId);
    cmd.add_option(
        dpp::command_option(dpp::co_string, "mode", "what to loop", true)
            .add_choice(dpp::command_option_choice("song", std::string("song")))
            .add_choice(dpp::command_option_choice("queue", std::string("queue")))
            .add_choice(dpp::command_option_choice("off", std::string("off")))
    );
    return cmd;
}

void LoopCommand::execute(const dpp::slashcommand_t &event)
{
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);
    if (currentVoiceChannel && !VoiceConnector::userInBotChannel(event) && !VoiceConnector::botIsAloneInChannel(event)) {
        event.reply(messages::mustBeWithBot);
        return;
    }

    std::string mode;
    auto modeParameter = event.get_parameter("mode");
    if (std::holds_alternative<std::string>(modeParameter)) {
        mode = std::get<std::string>(modeParameter);
    }

    if (mode == "song") {
        playback->setLoopMode(PlaybackController::LoopMode::Song);
        event.reply(messages::loopSong);
    } else if (mode == "queue") {
        playback->setLoopMode(PlaybackController::LoopMode::Queue);
        event.reply(messages::loopQueue);
    } else {
        playback->setLoopMode(PlaybackController::LoopMode::Off);
        event.reply(messages::loopOff);
    }
}
