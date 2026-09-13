#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;

// Decodes one media url into 48kHz s16 stereo PCM, handed out as packets of
// a fixed size. Knows nothing about threads, queues, or Discord.
class PcmResampler
{
public:
    enum class Result { Ok, OpenFailed, ReadFailed, NoAudio };
    using PacketSink = std::function<void(std::vector<uint8_t>)>;

    explicit PcmResampler(size_t packetBytes_);
    ~PcmResampler();

    PcmResampler(const PcmResampler &) = delete;
    PcmResampler &operator=(const PcmResampler &) = delete;

    // Opens the stream and sets up the decoder and resampler; nothing is
    // read until run(). Everything allocated here is freed by the destructor.
    Result open(const std::string &directUrl);

    // Runs until the stream ends or keepGoing() turns false. Every packet is
    // exactly packetBytes; only the last may be shorter.
    void run(const PacketSink &sink, const std::function<bool()> &keepGoing);

private:
    AVFormatContext *format{nullptr};
    AVCodecContext *codecContext{nullptr};
    SwrContext *swr{nullptr};
    int audioStream{-1};
    size_t packetBytes;
};
