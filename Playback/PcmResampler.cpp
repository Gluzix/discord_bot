#include "PcmResampler.h"

#include "ChunkedSource.h"

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/rational.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
}

PcmResampler::PcmResampler(size_t packetBytes_)
    : packetBytes(packetBytes_)
{
}

PcmResampler::~PcmResampler()
{
    auto asMs = [](std::chrono::steady_clock::duration d) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    };
    if (readTime != std::chrono::steady_clock::duration::zero()) {
        qDebug() << "Total read (network) time: " << asMs(readTime) << "ms";
        qDebug() << "Total decode/resample time: " << asMs(decodeTime - sinkTime) << "ms";
        qDebug() << "Total wait on the PCM queue: " << asMs(sinkTime) << "ms";
    }

    swr_free(&swr);
    avcodec_free_context(&codecContext);
    avformat_close_input(&format); // leaves our pb alone (AVFMT_FLAG_CUSTOM_IO)
    if (avio) {
        av_freep(&avio->buffer);
        avio_context_free(&avio);
    }
}

PcmResampler::Result PcmResampler::open(const std::string &directUrl)
{
    source = std::make_unique<ChunkedSource>();
    source->url = directUrl;
    av_dict_set(&source->httpOptions, "headers",
                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                0);

    const int IO_BUFFER_BYTES = 64 * 1024;
    avio = avio_alloc_context(static_cast<uint8_t *>(av_malloc(IO_BUFFER_BYTES)), IO_BUFFER_BYTES, 0,
                              source.get(), &ChunkedSource::readCallback, nullptr, &ChunkedSource::seekCallback);
    format = avformat_alloc_context();
    format->pb = avio;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    int errorCode = avformat_open_input(&format, directUrl.c_str(), nullptr, nullptr);

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

    const AVStream *stream = format->streams[audioStream];
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
        durationFromContainer = static_cast<double>(format->duration) / AV_TIME_BASE;
    } else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        durationFromContainer = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    }

    const AVCodecParameters *params = stream->codecpar;
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

void PcmResampler::requestSeek(double seconds, uint64_t ticket)
{
    std::lock_guard<std::mutex> lock(pendingSeekMutex);
    pendingSeek = PendingSeek{std::max(seconds, 0.0), ticket, true};
}

double PcmResampler::durationSeconds() const
{
    return durationFromContainer;
}

int64_t PcmResampler::secondsToStreamTs(double seconds) const
{
    const AVStream *stream = format->streams[audioStream];
    int64_t ts = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
    if (stream->start_time != AV_NOPTS_VALUE) {
        ts += stream->start_time;
    }
    return ts;
}

double PcmResampler::streamTsToSeconds(int64_t ts) const
{
    const AVStream *stream = format->streams[audioStream];
    if (stream->start_time != AV_NOPTS_VALUE) {
        ts -= stream->start_time;
    }
    return static_cast<double>(ts) * av_q2d(stream->time_base);
}

bool PcmResampler::frameEndsBy(const AVFrame *frame, double seconds) const
{
    if (frame->best_effort_timestamp == AV_NOPTS_VALUE) {
        return false;
    }
    const int sampleRate = frame->sample_rate > 0 ? frame->sample_rate : codecContext->sample_rate;
    if (sampleRate <= 0) {
        return false;
    }
    const double end = streamTsToSeconds(frame->best_effort_timestamp)
        + static_cast<double>(frame->nb_samples) / sampleRate;
    return end <= seconds;
}

PcmResampler::RunEnd PcmResampler::run(const PacketSink &sink, const std::function<bool()> &keepGoing, const SeekDone &onSeeked)
{
    Q_ASSERT(sink && keepGoing); // callers must wire both
    if (!sink || !keepGoing) {
        return RunEnd::Stopped;
    }
    if (!keepGoing()) {
        return RunEnd::Stopped; // skip or stop landed between open() and run()
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
            auto sinkStart = std::chrono::steady_clock::now();
            sink(std::vector<uint8_t>(staging.begin() + readPos, staging.begin() + readPos + packetBytes));
            sinkTime += (std::chrono::steady_clock::now() - sinkStart);
            readPos += packetBytes;
        }
        staging.erase(staging.begin(), staging.begin() + readPos);
    };

    // A container seek lands on the cue at or before the target, which can be
    // seconds early; while this is set, frames up to the target are dropped.
    double skipUntilSeconds = -1.0;

    auto applyPendingSeek = [&]() {
        PendingSeek request;
        {
            std::lock_guard<std::mutex> lock(pendingSeekMutex);
            if (!pendingSeek.set) {
                return;
            }
            request = pendingSeek;
            pendingSeek.set = false;
        }

        auto seekStart = std::chrono::steady_clock::now();
        const bool ok = av_seek_frame(format, audioStream, secondsToStreamTs(request.seconds),
                                      AVSEEK_FLAG_BACKWARD) >= 0;
        if (ok) {
            avcodec_flush_buffers(codecContext); // also clears the decoder's end-of-stream state
            swr_init(swr);                       // drops the resampler's delay line
            staging.clear();
            skipUntilSeconds = request.seconds;
        }
        auto tookMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - seekStart).count();
        qDebug() << "Seek to" << request.seconds << "s (ticket" << request.ticket << ") ok:" << ok
                 << "in" << tookMs << "ms";
        if (onSeeked) {
            onSeeked(request.ticket, ok);
        }
    };

    RunEnd end = RunEnd::Stopped;

    while (keepGoing()) {
        applyPendingSeek();

        auto t0 = std::chrono::steady_clock::now();
        int ret = av_read_frame(format, packet);
        auto t1 = std::chrono::steady_clock::now();
        readTime += (t1 - t0);

        if (ret < 0) {
            end = ret == AVERROR_EOF ? RunEnd::EndOfStream : RunEnd::ReadError;
            break;
        }

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
            if (skipUntilSeconds >= 0.0 && frameEndsBy(frame, skipUntilSeconds)) {
                av_frame_unref(frame);
                continue;
            }
            skipUntilSeconds = -1.0;
            convertIntoStaging((const uint8_t**)frame->data, frame->nb_samples);
            pushFullPackets();
            av_frame_unref(frame);
        }
        auto t3 = std::chrono::steady_clock::now();
        decodeTime += (t3 - t2);

        av_packet_unref(packet);
    }

    // Drain the resampler; only this last packet may be shorter than
    // packetBytes - DPP silence-pads it, inaudible at end of stream.
    auto drainStart = std::chrono::steady_clock::now();
    convertIntoStaging(nullptr, 0);
    pushFullPackets();
    if (!staging.empty()) {
        auto sinkStart = std::chrono::steady_clock::now();
        sink(std::move(staging));
        sinkTime += (std::chrono::steady_clock::now() - sinkStart);
    }
    decodeTime += (std::chrono::steady_clock::now() - drainStart);

    av_packet_free(&packet);
    av_frame_free(&frame);
    return end;
}
