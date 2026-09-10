#include "StopCommand.h"
#include "PlaybackController.h"

#include <dpp/dpp.h>

StopCommand::StopCommand(std::shared_ptr<PlaybackController> playback_)
    : Command("stop", "stop playing music")
    , playback(playback_)
{
}

void StopCommand::execute(const dpp::slashcommand_t &event)
{
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);

    if (!currentVoiceChannel || !currentVoiceChannel->voiceclient || !currentVoiceChannel->voiceclient->is_ready()) {
        event.reply("I'm not connected to any voice channel!");
        return;
    }

    bool wasPlaying = currentVoiceChannel->voiceclient->is_playing();

    // Stop the decoder/sender threads regardless, so nothing keeps refilling
    // the queue even if DPP happened to be momentarily drained.
    playback->stop();

    if (!wasPlaying) {
        event.reply("Nothing is playing right now!");
        return;
    }

    // Discard everything already encoded and queued inside DPP, otherwise
    // playback continues until its internal queue drains.
    currentVoiceChannel->voiceclient->stop_audio();

    event.reply("Stopped playing.");
}
