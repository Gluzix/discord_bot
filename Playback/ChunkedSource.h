#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct AVDictionary;

// Feeds the demuxer from memory, fetching the url in bounded ranges.
// =======================================================
// Rules:
// - googlevideo serves an open-ended read at about twice the audio bitrate
//   but a bounded range at full speed (the reason yt-dlp downloads in
//   chunks), so every fetch is one complete bounded request.
// - FFmpeg's own reconnect option is not used: it treats the end of a
//   bounded range as a premature end (it knows the whole file's size) and
//   burns seconds retrying, so a short chunk is retried here instead.
// =======================================================
class ChunkedSource
{
public:
    ChunkedSource();
    static constexpr int64_t FIRST_CHUNK_BYTES = 256 * 1024;  // small: audio starts fast even on a slow link
    static constexpr int64_t CHUNK_BYTES = 2 * 1024 * 1024;   // ~2 minutes of 128kbps audio

    std::string url;
    AVDictionary *httpOptions{nullptr};
    int64_t fileSize{-1};       // from the first response's Content-Range
    int64_t position{0};        // next byte the demuxer will read
    int64_t chunkStart{0};
    std::vector<uint8_t> chunk; // bytes [chunkStart, chunkStart + chunk.size())

    ~ChunkedSource();

    static int readCallback(void *opaque, uint8_t *buf, int size);
    static int64_t seekCallback(void *opaque, int64_t offset, int whence);

    bool fetchChunkAt(int64_t offset);
    int read(uint8_t *buf, int size);
    int64_t seek(int64_t offset, int whence);
};
