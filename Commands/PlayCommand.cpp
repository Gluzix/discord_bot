#include "PlayCommand.h"

#include <dpp/dpp.h>
#include <QDebug>

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

    // encoder.openFile();

    resamplingThread = std::thread(&PlayCommand::pcmResample, this);
    resamplingThread.detach();

    audioThread = std::thread(&PlayCommand::streamAudio, this, currentVoiceChannel);
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

std::vector<uint8_t> accumulator;

void PlayCommand::streamAudio(dpp::voiceconn *vc)
{
    const int CHUNK_SIZE = 384000; // 2s of data
    const int FOUR_BYTE_ALIGNMENT = 4;
    size_t readOffset = 0;
    isPlaying = true;

    while (isPlaying) {
        std::vector<uint8_t> chunk;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] { return !audioQueue.empty() || decodingFinished; });
            if (audioQueue.empty() && decodingFinished && accumulator.size() < CHUNK_SIZE) break;

            if (!audioQueue.empty()) {
                chunk = std::move(audioQueue.front());
                audioQueue.pop();
            }
        }

        accumulator.insert(accumulator.end(), chunk.begin(), chunk.end());

        while (accumulator.size() - readOffset >= CHUNK_SIZE) {
            vc->voiceclient->send_audio_raw((uint16_t*)(accumulator.data() + readOffset), CHUNK_SIZE);
            readOffset += CHUNK_SIZE;
        }

        if (readOffset > CHUNK_SIZE * 4) {
            accumulator.erase(accumulator.begin(), accumulator.begin() + readOffset);
            readOffset = 0;
        }
    }

    size_t remaining = accumulator.size() - readOffset;
    if (remaining > 0) {
        size_t alignedSize = remaining - (remaining % FOUR_BYTE_ALIGNMENT);
        if (alignedSize > 0) {
            vc->voiceclient->send_audio_raw((uint16_t*)(accumulator.data() + readOffset), alignedSize);
        }
    }

    // Send a brief silence to cleanly stop instead of cutting off abruptly
    if (!isPlaying) {
        std::vector<uint8_t> silence(CHUNK_SIZE, 0);
        vc->voiceclient->send_audio_raw((uint16_t*)silence.data(), CHUNK_SIZE);
    }

    isPlaying = false;
}

void PlayCommand::pcmResample()
{
    pcmData.clear();

    AVDictionary *options = nullptr;
    av_dict_set(&options, "headers",
                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                0);
    av_dict_set(&options, "buffer_size", "1048576", 0); // 1MB read buffer
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    AVFormatContext *format = nullptr;
    int errorCode = avformat_open_input(&format, "https://rr2---sn-cxn3pqhxqp5-3g3l.googlevideo.com/videoplayback?expire=1785189326&ei=bn9nasu7M6Xi6dsPzPjIiQY&ip=109.95.112.195&id=o-ALSJNX28kT9ZN8ucxa38ytuHNCPKZpH49ro1spR7FKuI&itag=251&source=youtube&requiressl=yes&xpc=EgVo2aDSNQ%3D%3D&cps=850&met=1785167726%2C&mh=zj&mm=31%2C29&mn=sn-cxn3pqhxqp5-3g3l%2Csn-f5f7knee&ms=au%2Crdu&mv=m&mvi=2&pl=21&rms=au%2Cau&initcwndbps=3352500&bui=AZFlqhMG_7u79g4RWu4YuLPab3Apk-X9H2xdM27x4IAlHu5M4Lv2qM4NKGZJysEQgzdZ635HREN_W88E&spc=SQ-umnIEILhQS-QOINhZkH5kB7768a6nafwv20uGhaWS&vprv=1&svpuc=1&mime=audio%2Fwebm&rqh=1&gir=yes&clen=174757070&dur=10821.781&lmt=1711912343475561&mt=1785167357&fvip=5&keepalive=yes&fexp=51565116%2C51992867&c=ANDROID_VR&txp=1308224&sparams=expire%2Cei%2Cip%2Cid%2Citag%2Csource%2Crequiressl%2Cxpc%2Cbui%2Cspc%2Cvprv%2Csvpuc%2Cmime%2Crqh%2Cgir%2Cclen%2Cdur%2Clmt&sig=AE0s2JYwRQIgHiHOdgi9E-QPhAgRBbIAArv-1UbrCElGBYxVMSOk74UCIQC9AnZM7nfHJc0We14EeK3lJJ4XUqJ_tVT7YUamoDl2gQ%3D%3D&lsparams=cps%2Cmet%2Cmh%2Cmm%2Cmn%2Cms%2Cmv%2Cmvi%2Cpl%2Crms%2Cinitcwndbps&lsig=APaTxxMwRAIgK3MhwtvFNhNQ06HbEvBfpD3xrVmU-aIZZ4J0HUeUyKkCIEavrjHAfeN0wosEKhR7-eA7QmfrXVv3Ub9ohQZwkqy7", nullptr, &options);

    if (errorCode < 0) {
        char errbuf[256];
        av_strerror(errorCode, errbuf, sizeof(errbuf));
        std::cerr << "avformat_open_input failed: " << errbuf << " (code: " << errorCode << ")\n";
    }

    if (errorCode != 0) {
        qDebug() << "Cannot open file! avformat_open_input returned with " << errorCode;
    }
    errorCode = avformat_find_stream_info(format, nullptr);
    if (errorCode != 0) {
        qDebug() << "Cannot find stream info! avformat_find_stream_info returned with " << errorCode;
    }

    int audioStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStream < 0) {
        qDebug() << "Couldn't find audio stream! av_find_best_stream returned with " << audioStream;
    }

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

    while(av_read_frame(format, packet) >= 0) {

        if(packet->stream_index != audioStream) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(codec_ctx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }

        int maxOutSamples = swr_get_out_samples(swr, codec_ctx->frame_size > 0 ? codec_ctx->frame_size : 4096);
        int maxBufSize = av_samples_get_buffer_size(nullptr, 2, maxOutSamples, AV_SAMPLE_FMT_S16, 1);
        uint8_t *out_buf = (uint8_t*)av_malloc(maxBufSize * 2); // some headroom

        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
            int out_samples = swr_get_out_samples(swr, frame->nb_samples);

            int out_buf_size = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 1);
            if (out_buf_size > maxBufSize) {
                av_free(out_buf);
                out_buf = (uint8_t*)av_malloc(out_buf_size);
            }

            int samples_converted = swr_convert(
                swr,
                &out_buf,
                out_samples,
                (const uint8_t**)frame->data,
                frame->nb_samples
                );

            if (samples_converted > 0) {
                int actual_size = av_samples_get_buffer_size(nullptr, 2, samples_converted, AV_SAMPLE_FMT_S16,1);

                std::vector<uint8_t> chunk(out_buf, out_buf + actual_size);
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    audioQueue.push(std::move(chunk));
                }
                queueCv.notify_one();
            }
            // av_free(out_buf);
            av_frame_unref(frame);
        }
        av_free(out_buf);

        av_packet_unref(packet);
    }

    uint8_t *out_buf = nullptr;
    int remaining = swr_get_out_samples(swr, 0);
    if (remaining > 0) {
        int out_buf_size = av_samples_get_buffer_size(nullptr, 2, remaining, AV_SAMPLE_FMT_S16, 1);
        out_buf = (uint8_t*)av_malloc(out_buf_size);

        int flushed = swr_convert(swr, &out_buf, remaining, nullptr, 0);
        if (flushed >0) {
            int actual_size = av_samples_get_buffer_size(nullptr, 2, flushed, AV_SAMPLE_FMT_S16, 1);

            std::vector<uint8_t> chunk(out_buf, out_buf + actual_size);
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                audioQueue.push(std::move(chunk));
            }
            queueCv.notify_one();
        }
        av_free(out_buf);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = true;
    }
    queueCv.notify_one();
}
