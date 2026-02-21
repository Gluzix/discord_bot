#include "Mp3Encoder.h"
#include <QDebug>

extern "C" {
    #include <libavutil/frame.h>
    #include <libavutil/mem.h>
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswresample/swresample.h>
}

Mp3Encoder::Mp3Encoder() {}

void Mp3Encoder::openFile()
{
}

void Mp3Encoder::PcmResample()
{
    AVFormatContext *format = nullptr;
    int errorCode = avformat_open_input(&format, "C:/workspace/discord_bot/wash-it-all-away.mp3", nullptr, nullptr);

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

        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
            int out_samples = swr_get_out_samples(swr, frame->nb_samples);

            uint8_t *out_buf = nullptr;
            int out_buf_size = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 1);

            out_buf = (uint8_t*)av_malloc(out_buf_size);

            int samples_converted = swr_convert(
                swr,
                &out_buf,
                out_samples,
                (const uint8_t**)frame->data,
                frame->nb_samples
            );

            if (samples_converted > 0) {
                int actual_size = av_samples_get_buffer_size(
                    nullptr, 2, samples_converted, AV_SAMPLE_FMT_S16,1
                );

                pcmData.insert(pcmData.end(), out_buf, out_buf + actual_size);
            }

            av_free(out_buf);
            av_frame_unref(frame);
        }

        av_packet_unref(packet);
    }

    uint8_t *out_buf = nullptr;
    int remaining = swr_get_out_samples(swr, 0);
    if (remaining > 0) {
        int out_buf_size = av_samples_get_buffer_size(nullptr, 2, remaining, AV_SAMPLE_FMT_S16, 1);
        out_buf = (uint8_t*)av_malloc(out_buf_size);

        int flushed = swr_convert(swr, &out_buf, remaining, nullptr, 0);
        if (flushed >0) {
            int actual_size = av_samples_get_buffer_size(
                nullptr, 2, flushed, AV_SAMPLE_FMT_S16, 1
            );
            pcmData.insert(pcmData.end(), out_buf, out_buf + actual_size);
        }
        av_free(out_buf);
    }

}

void Mp3Encoder::saveFileAsPcm()
{
    FILE *f = fopen("output.pcm", "wb");
    fwrite(pcmData.data(), 1, pcmData.size(), f);
    fclose(f);
}
