#include "Handler.h"

#include <windows.h>
#include <iostream>
#include <QDebug>
#include "WindowsProcessRunner.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

struct PipeReaderCtx {
    HANDLE pipeHandle;
};

static int readPacketCallback(void* opaque, uint8_t* buf, int bufSize) {
    auto* ctx = static_cast<PipeReaderCtx*>(opaque);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(ctx->pipeHandle, buf, bufSize, &bytesRead, nullptr);
    if (!ok || bytesRead == 0) {
        return AVERROR_EOF;
    }
    return static_cast<int>(bytesRead);
}

void Handler::resample()
{
    HANDLE pipeHandle;
    WindowsProcessRunner runner("some_url_here", pipeHandle);
    runner.launchYtDlp();

    pcmData.clear();

    PipeReaderCtx pipeCtx{ pipeHandle };
    const int bufferSize = 32768;
    uint8_t* avioBuffer = (uint8_t*)av_malloc(bufferSize);

    AVIOContext* avioCtx = avio_alloc_context(
        avioBuffer, bufferSize,
        0,              // write flag = 0 (read-only)
        &pipeCtx,        // opaque, passed to callback
        &readPacketCallback,
        nullptr,        // no write callback
        nullptr         // no seek callback (stdin/pipe can't seek)
        );

    AVFormatContext* format = avformat_alloc_context();
    format->pb = avioCtx;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;

    int errorCode = avformat_open_input(&format, nullptr, nullptr, nullptr);

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

    auto readTime = std::chrono::steady_clock::duration::zero();
    auto decodeTime = std::chrono::steady_clock::duration::zero();

    while(true/*av_read_frame(format, packet) >= 0*/) {

        auto t0 = std::chrono::steady_clock::now();
        int ret = av_read_frame(format, packet);
        auto t1 = std::chrono::steady_clock::now();
        readTime += (t1 - t0);

        if (ret < 0) break;

        if(packet->stream_index != audioStream) {
            av_packet_unref(packet);
            continue;
        }

        auto t2 = std::chrono::steady_clock::now();
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
        auto t3 = std::chrono::steady_clock::now();
        decodeTime += (t3 - t2);

        qDebug() << "Total read (network) time: " << std::chrono::duration_cast<std::chrono::milliseconds>(readTime).count() << "ms";
        qDebug() << "Total decode/resample time: " << std::chrono::duration_cast<std::chrono::milliseconds>(decodeTime).count() << "ms";

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
