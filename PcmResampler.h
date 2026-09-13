#pragma once

#include <queue>
#include <condition_variable>
#include <functional>

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
    PcmResampler(std::queue<std::vector<uint8_t>> &audioQueue_,
                 std::atomic<bool> &isPlaying_,
                 std::condition_variable &queueCv_,
                 std::mutex &queueMutex_,
                 bool &currentSongFailed_,
                 bool &currentSongIsLoopReplay_,
                 std::string requestedUrl_);
    void setNotifyUser(const std::function<void(dpp::message msg)> &notifyUser_);
    void setSignalFinished(std::function<void()> &signalFinished_);

    void pcmResample(dpp::slashcommand_t event, const ResolvedMedia &media);
private:

    std::function<void(dpp::message msg)> notifyUser{nullptr};
    std::function<void()> signalFinished{nullptr};

    std::queue<std::vector<uint8_t>> &audioQueue;
    std::atomic<bool> &isPlaying;
    std::condition_variable &queueCv;
    std::mutex &queueMutex;
    bool &currentSongFailed;
    bool &currentSongIsLoopReplay;
    std::string requestedUrl;
};
