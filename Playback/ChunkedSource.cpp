#include "ChunkedSource.h"

#include <QDebug>

#include <thread>

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <chrono>

extern "C" {
#include <libavutil/dict.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

ChunkedSource::ChunkedSource() {}

ChunkedSource::~ChunkedSource()
{
    av_dict_free(&httpOptions);
}

int ChunkedSource::readCallback(void *opaque, uint8_t *buf, int size)
{
    return static_cast<ChunkedSource *>(opaque)->read(buf, size);
}

int64_t ChunkedSource::seekCallback(void *opaque, int64_t offset, int whence)
{
    return static_cast<ChunkedSource *>(opaque)->seek(offset, whence);
}

bool ChunkedSource::fetchChunkAt(int64_t offset)
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

int ChunkedSource::read(uint8_t *buf, int size)
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

int64_t ChunkedSource::seek(int64_t offset, int whence)
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
