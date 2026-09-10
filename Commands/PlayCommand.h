#pragma once

#include "JoinCommand.h"

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <memory>

namespace dpp {
class discord_voice_client;
struct voice_ready_t;
}

class PlayCommand : public JoinCommand
{
public:
    PlayCommand(std::string name, std::string reply);
    ~PlayCommand();

    void execute(const dpp::slashcommand_t &event) override;
    std::string name() override;
    std::string getReply() override;
    void stopSendingData();
    void stopPlayback();

    // Called by the bot's on_voice_ready handler; starts a /play request
    // that was waiting for the voice connection to finish its handshake.
    void onVoiceReady(const dpp::voice_ready_t &event);

private:
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void pcmResample(dpp::slashcommand_t event);
    void startPlayback(dpp::discord_voice_client *voiceClient, const std::string &url, const dpp::slashcommand_t &event);

    std::atomic<bool> isPlaying{false};

    std::thread audioThread;
    std::thread resamplingThread;

    std::string requestedUrl;
    std::queue<std::vector<uint8_t>> audioQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;

    // A /play issued while the voice connection is still being established.
    std::mutex pendingMutex;
    std::unique_ptr<dpp::slashcommand_t> pendingEvent;
    std::string pendingUrl;
    uint64_t pendingGuildId{0};
};
