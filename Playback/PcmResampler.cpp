#include "PcmResampler.h"

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libswresample/swresample.h>
}

// googlevideo serves an open-ended read at about twice the audio bitrate, but
// a bounded range at full speed (the reason yt-dlp downloads in chunks). So
// the file is fetched in bounded ranges through FFmpeg's own http client, one
// complete request per chunk, and the demuxer reads from memory.
struct PcmResampler::ChunkedSource
{
    static constexpr int64_t FIRST_CHUNK_BYTES = 256 * 1024;  // small: audio starts fast even on a slow link
    static constexpr int64_t CHUNK_BYTES = 2 * 1024 * 1024;   // ~2 minutes of 128kbps audio

    std::string url;
    AVDictionary *httpOptions{nullptr};
    int64_t fileSize{-1};       // from the first response's Content-Range
    int64_t position{0};        // next byte the demuxer will read
    int64_t chunkStart{0};
    std::vector<uint8_t> chunk; // bytes [chunkStart, chunkStart + chunk.size())

    ~ChunkedSource()
    {
        av_dict_free(&httpOptions);
    }

    static int readCallback(void *opaque, uint8_t *buf, int size)
    {
        return static_cast<ChunkedSource *>(opaque)->read(buf, size);
    }

    static int64_t seekCallback(void *opaque, int64_t offset, int whence)
    {
        return static_cast<ChunkedSource *>(opaque)->seek(offset, whence);
    }

    // One bounded request. FFmpeg's own reconnect option is not used: it
    // treats the end of a bounded range as a premature end (it knows the
    // whole file's size) and burns seconds retrying, so a short chunk is
    // retried here instead.
    bool fetchChunkAt(int64_t offset)
    {
        for (int attempt = 0; attempt < 3; ++attempt) {
            int64_t end = offset + (fileSize < 0 ? FIRST_CHUNK_BYTES : CHUNK_BYTES);
            if (fileSize >= 0) {
                end = std::min(end, fileSize); // a range past the file is a premature end to FFmpeg too
            }

            AVDictionary *options = nullptr;
            av_dict_copy(&options, httpOptions, 0);
            av_dict_set_int(&options, "offset", offset, 0);
            av_dict_set_int(&options, "end_offset", end, 0);
            AVIOContext *http = nullptr;
            int errorCode = avio_open2(&http, url.c_str(), AVIO_FLAG_READ, nullptr, &options);
            av_dict_free(&options);
            if (errorCode < 0) {
                char errbuf[256];
                av_strerror(errorCode, errbuf, sizeof(errbuf));
                qDebug() << "Chunk fetch at" << offset << "failed:" << errbuf;
                return false;
            }
            if (fileSize < 0) {
                int64_t reported = avio_size(http); // the Content-Range total, not the chunk
                if (reported > 0) {
                    fileSize = reported;
                }
            }
            const int64_t expectedEnd = fileSize >= 0 ? std::min(end, fileSize) : end;

            chunk.clear();
            chunk.reserve(static_cast<size_t>(expectedEnd - offset));
            uint8_t buffer[64 * 1024];
            for (;;) {
                int n = avio_read(http, buffer, sizeof(buffer));
                if (n <= 0) {
                    break;
                }
                chunk.insert(chunk.end(), buffer, buffer + n);
            }
            avio_closep(&http);
            chunkStart = offset;

            const int64_t got = offset + static_cast<int64_t>(chunk.size());
            if (got >= expectedEnd) {
                return true;
            }
            if (fileSize < 0) {
                fileSize = got; // size never reported: a short chunk is the end
                return true;
            }
            qDebug() << "Chunk at" << offset << "ended early, retrying";
            std::this_thread::sleep_for(std::chrono::milliseconds(200 * (attempt + 1)));
        }
        return false;
    }

    int read(uint8_t *buf, int size)
    {
        if (fileSize >= 0 && position >= fileSize) {
            return AVERROR_EOF;
        }
        const bool inChunk = position >= chunkStart && position < chunkStart + static_cast<int64_t>(chunk.size());
        if (!inChunk) {
            if (!fetchChunkAt(position)) {
                return AVERROR(EIO);
            }
            if (chunk.empty()) {
                return AVERROR_EOF;
            }
        }
        const size_t offsetInChunk = static_cast<size_t>(position - chunkStart);
        const int n = static_cast<int>(std::min(static_cast<size_t>(size), chunk.size() - offsetInChunk));
        std::memcpy(buf, chunk.data() + offsetInChunk, static_cast<size_t>(n));
        position += n;
        return n;
    }

    int64_t seek(int64_t offset, int whence)
    {
        whence &= ~AVSEEK_FORCE;
        if (whence == AVSEEK_SIZE) {
            return fileSize >= 0 ? fileSize : AVERROR(ENOSYS);
        }
        int64_t target = 0;
        switch (whence) {
            case SEEK_SET: target = offset; break;
            case SEEK_CUR: target = position + offset; break;
            case SEEK_END:
                if (fileSize < 0) {
                    return AVERROR(ENOSYS);
                }
                target = fileSize + offset;
                break;
            default: return AVERROR(EINVAL);
        }
        if (target < 0) {
            return AVERROR(EINVAL);
        }
        position = target; // the next read fetches whatever chunk that lands in
        return position;
    }
};

PcmResampler::PcmResampler(size_t packetBytes_)
    : packetBytes(packetBytes_)
{
}

PcmResampler::~PcmResampler()
{
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
