#pragma once

#include "JoinCommand.h"
#include "Mp3Encoder.h"

#include <atomic>
#include <thread>

namespace dpp {
class voiceconn;
}

class PlayCommand : public JoinCommand
{
public:
    PlayCommand(std::string name, std::string reply);

    void execute(const dpp::slashcommand_t &event) override;
    std::string name() override;
    std::string getReply() override;
    void stopSendingData();

private:
    Mp3Encoder encoder;
    void streamAudio(dpp::voiceconn* vc, const std::vector<uint8_t>& pcmData);

    std::atomic<bool> isPlaying{false};
    std::thread audioThread;
};
