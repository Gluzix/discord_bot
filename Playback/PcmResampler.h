#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVIOContext;
struct SwrContext;

class ChunkedSource;

// Decodes one media url into 48kHz s16 stereo PCM, handed out as packets of
// a fixed size. Knows nothing about threads, queues, or Discord.
class PcmResampler
{
public:
    enum class Result { Ok, OpenFailed, ReadFailed, NoAudio };
    enum class RunEnd { EndOfStream, ReadError, Stopped };
    using PacketSink = std::function<void(std::vector<uint8_t>)>;

    // Reports the ticket of the seek that was just applied, so the caller can
    // tell its latest request from an older one.
    using SeekDone = std::function<void(uint64_t ticket, bool ok)>;

    explicit PcmResampler(size_t packetBytes_);
    ~PcmResampler();

    PcmResampler(const PcmResampler &) = delete;
    PcmResampler &operator=(const PcmResampler &) = delete;

    // Opens the stream and sets up the decoder and resampler; nothing is
    // read until run(). Everything allocated here is freed by the destructor.
    Result open(const std::string &directUrl);

    // Runs until the stream ends or keepGoing() turns false, and says which.
    // Every packet is exactly packetBytes; only the last may be shorter.
    // Restartable: after it returned at end of stream, a new run() honours a
    // seek requested meanwhile and decodes again.
    RunEnd run(const PacketSink &sink, const std::function<bool()> &keepGoing, const SeekDone &onSeeked);

    // Asks run() to jump to `seconds`; callable from any thread, and also
    // while run() is not running (the next run() starts there). A newer
    // request overwrites an unconsumed older one.
    void requestSeek(double seconds, uint64_t ticket);

    // 0 when the container doesn't say.
    double durationSeconds() const;

private:
    struct PendingSeek
    {
        double seconds{0};
        uint64_t ticket{0};
        bool set{false};
    };

    int64_t secondsToStreamTs(double seconds) const;
    double streamTsToSeconds(int64_t ts) const;

    // False for a frame without a timestamp: kept rather than guessed away.
    bool frameEndsBy(const AVFrame *frame, double seconds) const;

    std::unique_ptr<ChunkedSource> source;
    AVIOContext *avio{nullptr};
    AVFormatContext *format{nullptr};
    AVCodecContext *codecContext{nullptr};
    SwrContext *swr{nullptr};
    int audioStream{-1};
    size_t packetBytes;
    double durationFromContainer{0};

    PendingSeek pendingSeek;
    std::mutex pendingSeekMutex;

    // Accumulated across runs and reported once, in the destructor.
    std::chrono::steady_clock::duration readTime{};
    std::chrono::steady_clock::duration decodeTime{};
    // The sink blocks whenever the bounded PCM queue is full, which is most
    // of a song - counting that as decode time makes decoding look endless.
    std::chrono::steady_clock::duration sinkTime{};
};
