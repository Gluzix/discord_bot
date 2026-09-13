#include "PcmResampler.h"

#include <QDebug>

#include <chrono>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

PcmResampler::PcmResampler(size_t packetBytes_)
    : packetBytes(packetBytes_)
{
}

PcmResampler::~PcmResampler()
{
    swr_free(&swr);
    avcodec_free_context(&codecContext);
    avformat_close_input(&format);
}

PcmResampler::Result PcmResampler::open(const std::string &directUrl)
{
    AVDictionary *options = nullptr;
    av_dict_set(&options, "headers",
                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                0);
    av_dict_set(&options, "buffer_size", "1048576", 0); // 1MB read buffer
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "multiple_requests", "1", 0);
    int errorCode = avformat_open_input(&format, directUrl.c_str(), nullptr, &options);
    av_dict_free(&options); // open_input consumed what it needed

    if (errorCode != 0) {
        char errbuf[256];
        av_strerror(errorCode, errbuf, sizeof(errbuf));
        qDebug() << "Cannot open input! avformat_open_input returned with " << errorCode << errbuf;
        return Result::OpenFailed;
    }

    errorCode = avformat_find_stream_info(format, nullptr);
    if (errorCode != 0) {
        qDebug() << "Cannot find stream info! avformat_find_stream_info returned with " << errorCode;
        return Result::ReadFailed;
    }

    audioStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStream < 0) {
        qDebug() << "Couldn't find audio stream! av_find_best_stream returned with " << audioStream;
        return Result::NoAudio;
    }

    const AVCodecParameters *params = format->streams[audioStream]->codecpar;
    const AVCodec *decoder = avcodec_find_decoder(params->codec_id);
    if (decoder == nullptr) {
        qDebug() << "No decoder for codec id" << params->codec_id;
        return Result::NoAudio;
    }
    codecContext = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codecContext, params);
    if (avcodec_open2(codecContext, decoder, nullptr) < 0) {
        qDebug() << "avcodec_open2 failed";
        return Result::NoAudio;
    }

    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, 48000,
                        &codecContext->ch_layout, codecContext->sample_fmt, codecContext->sample_rate,
                        0, nullptr);
    if (swr_init(swr) < 0) {
        qDebug() << "swr_init failed";
        return Result::NoAudio;
    }

    return Result::Ok;
}

void PcmResampler::run(const PacketSink &sink, const std::function<bool()> &keepGoing)
{
    Q_ASSERT(sink && keepGoing); // callers must wire both
    if (!sink || !keepGoing) {
        return;
    }
    if (!keepGoing()) {
        return; // skip or stop landed between open() and run()
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // Packets are exactly packetBytes. DPP drops the remainder of larger
    // sends and silence-pads smaller ones, so the invariant is enforced
    // here, at the single point of production.
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
        while (staging.size() - readPos >= packetBytes) {
            sink(std::vector<uint8_t>(staging.begin() + readPos, staging.begin() + readPos + packetBytes));
            readPos += packetBytes;
        }
        staging.erase(staging.begin(), staging.begin() + readPos);
    };

    auto readTime = std::chrono::steady_clock::duration::zero();
    auto decodeTime = std::chrono::steady_clock::duration::zero();

    while (keepGoing()) {
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
        if (avcodec_send_packet(codecContext, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }

        while (avcodec_receive_frame(codecContext, frame) >= 0) {
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
    // packetBytes - DPP silence-pads it, inaudible at end of stream.
    convertIntoStaging(nullptr, 0);
    pushFullPackets();
    if (!staging.empty()) {
        sink(std::move(staging));
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
}
