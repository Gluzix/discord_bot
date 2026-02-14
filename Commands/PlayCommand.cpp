#include "PlayCommand.h"

#include <dpp/dpp.h>

PlayCommand::PlayCommand(std::string name, std::string reply)
    : JoinCommand(name, reply)
{

}

void PlayCommand::execute(const dpp::slashcommand_t &event)
{
    JoinCommand::execute(event);

    /* Get the voice channel the bot is in, in this current guild. */
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);

    /* If the voice channel was invalid, or there is an issue with it, then tell the user. */
    if (!currentVoiceChannel || !currentVoiceChannel->voiceclient || !currentVoiceChannel->voiceclient->is_ready()) {
        event.reply("There was an issue with getting the voice channel. Make sure I'm in a voice channel!");
        return;
    }

    /* Tell the bot to play the sound file 'Robot.pcm' in the current voice channel. */
    // v->voiceclient->send_audio_raw((uint16_t*)robot, robot_size);  SEND HERE AN ACTUAL AUDIO

    event.reply("Played music.");
}

std::string PlayCommand::name()
{
    return cmdName;
}

std::string PlayCommand::getReply()
{
    return reply;
}
