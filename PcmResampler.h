#pragma once

#include <functional>
#include <string>
#include <queue>
#include <condition_variable>

class AVFormatContext;
class AVCodecContext;
class SwrContext;

namespace dpp {
class discord_voice_client;
struct voice_ready_t;
struct slashcommand_t;
struct message;
}

class ResolvedMedia;

class PcmResampler
{
public:
    enum class Result {Ok, OpenFailed, ReadFailed, NoAudio};
    using PacketSink = std::function<void(std::vector<uint8_t>)>;

    PcmResampler(size_t packetBytes_);
    ~PcmResampler();

    void pcmResample(dpp::slashcommand_t event, const ResolvedMedia &media);

    Result open(const std::string &directUrl);
    // Runs until the stream ends or keepGoing() turns false. Every packet is
    // exactly packetBytes; only the last may be shorter.
    void run(const PacketSink &sink, const std::function<bool()> &keepGoing);

private:
    // std::queue<std::vector<uint8_t>> &audioQueue;
    // std::condition_variable &queueCv;

    AVFormatContext *format{nullptr};
    AVCodecContext *codec_ctx{nullptr};
    SwrContext *swr{nullptr};
    int audioStream{-1};
    size_t packetBytes;
};
