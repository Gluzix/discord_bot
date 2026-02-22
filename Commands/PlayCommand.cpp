#include "PlayCommand.h"

#include <dpp/dpp.h>

PlayCommand::PlayCommand(std::string name, std::string reply)
    : JoinCommand(name, reply)
{

}

void PlayCommand::execute(const dpp::slashcommand_t &event)
{
    JoinCommand::execute(event);
    Sleep(3000);

    /* Get the voice channel the bot is in, in this current guild. */
    dpp::voiceconn* currentVoiceChannel = event.from()->get_voice(event.command.guild_id);

    /* If the voice channel was invalid, or there is an issue with it, then tell the user. */
    if (!currentVoiceChannel || !currentVoiceChannel->voiceclient || !currentVoiceChannel->voiceclient->is_ready()) {
        event.reply("There was an issue with getting the voice channel. Make sure I'm in a voice channel!");
        return;
    }

    encoder.openFile();
    encoder.PcmResample();

    audioThread = std::thread(&PlayCommand::streamAudio, this, currentVoiceChannel, std::ref(encoder.pcmData));
    audioThread.detach();

    // Stream audio in a separate thread

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

void PlayCommand::stopSendingData()
{
    isPlaying = false;
}

void PlayCommand::streamAudio(dpp::voiceconn *vc, const std::vector<uint8_t> &pcmData)
{
    const int CHUNK_SIZE = 384000; // 2s of data
    size_t offset = 0;

    isPlaying = true;

    while (isPlaying && offset + CHUNK_SIZE <= pcmData.size()) {
        vc->voiceclient->send_audio_raw(
            (uint16_t*)(pcmData.data() + offset),
            CHUNK_SIZE
        );
        offset += CHUNK_SIZE;
    }

    // Send a brief silence to cleanly stop instead of cutting off abruptly
    if (!isPlaying) {
        std::vector<uint8_t> silence(CHUNK_SIZE, 0);
        vc->voiceclient->send_audio_raw((uint16_t*)silence.data(), CHUNK_SIZE);
    }

    isPlaying = false;
}
