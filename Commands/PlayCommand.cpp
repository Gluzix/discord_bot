#include "PlayCommand.h"
#include "WindowsProcessRunner.h"

#include <dpp/dpp.h>
#include <QDebug>
#include <cctype>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}


PlayCommand::PlayCommand(std::string name, std::string reply)
    : JoinCommand(name, reply)
{

}

PlayCommand::~PlayCommand()
{
    stopPlayback();
}

// User input ends up on a yt-dlp command line; allow only plain YouTube
// links so nothing can break out of the quotes or inject extra arguments.
static bool isAllowedYoutubeUrl(const std::string &url)
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

    // JoinCommand already acknowledged the interaction, so any feedback from
    // here on goes through edit_original_response.
    std::string yturl;
    auto urlParameter = event.get_parameter("url");
    if (std::holds_alternative<std::string>(urlParameter)) {
        yturl = std::get<std::string>(urlParameter);
    }

    if (!isAllowedYoutubeUrl(yturl)) {
        event.edit_original_response(dpp::message("That doesn't look like a YouTube link I can play!"));
        return;
    }

    // encoder.openFile();

    // A previous session's threads may still be decoding the old song (they
    // used to outlive /leave); stop and join them, then discard whatever DPP
    // still has queued, so old packets can't mix into the new song.
    stopPlayback();
    currentVoiceChannel->voiceclient->stop_audio();

    // Reset leftovers from a previous play, otherwise streamAudio sees
    // decodingFinished == true and exits immediately. isPlaying must be set
    // before the threads start — both loops check it.
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = false;
        audioQueue = {};
    }
    requestedUrl = yturl;
    isPlaying = true;

    resamplingThread = std::thread(&PlayCommand::pcmResample, this, event);
    audioThread = std::thread(&PlayCommand::streamAudio, this, currentVoiceChannel);
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
    queueCv.notify_all();
}

void PlayCommand::stopPlayback()
{
    stopSendingData();

    // Join instead of abandoning the threads: a detached decoder outlives
    // /leave and keeps pushing the old song's packets into the queue, which
    // then interleave with the next song's.
    if (resamplingThread.joinable()) {
        resamplingThread.join();
    }
    if (audioThread.joinable()) {
        audioThread.join();
    }
}

void PlayCommand::streamAudio(dpp::voiceconn *vc)
{
    // The decoder fills the queue with ready-to-send packets of exactly
    // dpp::send_audio_raw_max_length bytes; only the final one may be shorter.
    while (isPlaying) {
        std::vector<uint8_t> packet;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] {
                return !audioQueue.empty() || decodingFinished || !isPlaying;
            });
            if (audioQueue.empty()) {
                if (decodingFinished) break;
                continue;
            }
            packet = std::move(audioQueue.front());
            audioQueue.pop();
        }

        vc->voiceclient->send_audio_raw((uint16_t*)packet.data(), packet.size());
    }

    isPlaying = false;
}

void PlayCommand::pcmResample(dpp::slashcommand_t event)
{
    // Whatever happens here, the sender thread waits on the queue and must
    // be released - every exit path has to mark decoding as finished.
    auto signalFinished = [this]() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            decodingFinished = true;
        }
        queueCv.notify_one();
    };

    std::string directUrl = WindowsProcessRunner::resolveDirectUrl(requestedUrl);
    if (directUrl.empty()) {
        event.edit_original_response(dpp::message("Couldn't get the audio from that link :("));
        signalFinished();
        return;
    }

    AVDictionary *options = nullptr;
    av_dict_set(&options, "headers",
                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                0);
    av_dict_set(&options, "buffer_size", "1048576", 0); // 1MB read buffer
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "multiple_requests", "1", 0);
    AVFormatContext *format = nullptr;
    int errorCode = avformat_open_input(&format, directUrl.c_str(), nullptr, &options);

    if (errorCode != 0) {
        char errbuf[256];
        av_strerror(errorCode, errbuf, sizeof(errbuf));
        qDebug() << "Cannot open input! avformat_open_input returned with " << errorCode << errbuf;
        event.edit_original_response(dpp::message("Couldn't open the audio stream :("));
        signalFinished();
        return;
    }

    errorCode = avformat_find_stream_info(format, nullptr);
    if (errorCode != 0) {
        qDebug() << "Cannot find stream info! avformat_find_stream_info returned with " << errorCode;
        avformat_close_input(&format);
        event.edit_original_response(dpp::message("Couldn't read the audio stream :("));
        signalFinished();
        return;
    }

    int audioStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStream < 0) {
        qDebug() << "Couldn't find audio stream! av_find_best_stream returned with " << audioStream;
        avformat_close_input(&format);
        event.edit_original_response(dpp::message("That link has no audio stream :("));
        signalFinished();
        return;
    }

    event.edit_original_response(dpp::message("Playing!"));

    const AVCodec *codec = avcodec_find_decoder(format->streams[audioStream]->codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codec_ctx, format->streams[audioStream]->codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    SwrContext* swr = nullptr;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_ch_layout = codec_ctx->ch_layout;

    swr_alloc_set_opts2(&swr, &out_ch_layout,
                        AV_SAMPLE_FMT_S16,
                        48000,
                        &in_ch_layout,
                        codec_ctx->sample_fmt,
                        codec_ctx->sample_rate,
                        0, nullptr);

    swr_init(swr);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // The queue carries ready-to-send packets of exactly PACKET_SIZE bytes.
    // DPP drops the remainder of larger sends and silence-pads smaller ones,
    // so the invariant is enforced here, at the single point of production.
    const size_t PACKET_SIZE = dpp::send_audio_raw_max_length;
    const size_t BYTES_PER_SAMPLE_PAIR = 4; // s16 stereo
    std::vector<uint8_t> staging;

    auto convertIntoStaging = [&](const uint8_t **inData, int inSamples) {
        int maxOutSamples = swr_get_out_samples(swr, inSamples);
        if (maxOutSamples <= 0) return;
        size_t writePos = staging.size();
        staging.resize(writePos + (size_t)maxOutSamples * BYTES_PER_SAMPLE_PAIR);
        uint8_t *outPlane = staging.data() + writePos;
        int converted = swr_convert(swr, &outPlane, maxOutSamples, inData, inSamples);
        staging.resize(writePos + (converted > 0 ? (size_t)converted * BYTES_PER_SAMPLE_PAIR : 0));
    };

    auto pushFullPackets = [&]() {
        size_t readPos = 0;
        while (staging.size() - readPos >= PACKET_SIZE) {
            std::vector<uint8_t> pkt(staging.begin() + readPos,
                                     staging.begin() + readPos + PACKET_SIZE);
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                audioQueue.push(std::move(pkt));
            }
            queueCv.notify_one();
            readPos += PACKET_SIZE;
        }
        staging.erase(staging.begin(), staging.begin() + readPos);
    };

    auto readTime = std::chrono::steady_clock::duration::zero();
    auto decodeTime = std::chrono::steady_clock::duration::zero();

    while (isPlaying) {
        auto t0 = std::chrono::steady_clock::now();
        int ret = av_read_frame(format, packet);
        auto t1 = std::chrono::steady_clock::now();
        readTime += (t1 - t0);

        if (ret < 0) break;

        if (packet->stream_index != audioStream) {
            av_packet_unref(packet);
            continue;
        }

        auto t2 = std::chrono::steady_clock::now();
        if (avcodec_send_packet(codec_ctx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }

        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
            convertIntoStaging((const uint8_t**)frame->data, frame->nb_samples);
            pushFullPackets();
            av_frame_unref(frame);
        }
        auto t3 = std::chrono::steady_clock::now();
        decodeTime += (t3 - t2);

        av_packet_unref(packet);
    }

    qDebug() << "Total read (network) time: " << std::chrono::duration_cast<std::chrono::milliseconds>(readTime).count() << "ms";
    qDebug() << "Total decode/resample time: " << std::chrono::duration_cast<std::chrono::milliseconds>(decodeTime).count() << "ms";

    // Drain the resampler; only this last packet may be shorter than
    // PACKET_SIZE — DPP silence-pads it, inaudible at end of stream.
    convertIntoStaging(nullptr, 0);
    pushFullPackets();
    if (!staging.empty()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            audioQueue.push(std::move(staging));
        }
        queueCv.notify_one();
    }

    signalFinished();
}
