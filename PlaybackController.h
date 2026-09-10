#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace dpp {
class discord_voice_client;
struct voice_ready_t;
struct slashcommand_t;
}

// Owns the whole playback pipeline for the bot's voice connection:
// yt-dlp resolution, FFmpeg decode/resample, and the paced hand-off to DPP.
// Commands stay thin and only talk to this interface.
class PlaybackController
{
public:
    PlaybackController() = default;
    ~PlaybackController();

    // Plays the given YouTube url, replacing whatever is currently playing.
    // If the voice handshake is still in flight, the request is parked and
    // started by onVoiceReady. Expects an already-acknowledged interaction;
    // feedback goes through edit_original_response.
    void play(const std::string &youtubeUrl, const dpp::slashcommand_t &event);

    // Called by the bot's on_voice_ready handler; starts a parked request
    // once the voice connection can accept audio.
    void onVoiceReady(const dpp::voice_ready_t &event);

    // Stops decoding/sending, joins the worker threads and clears any
    // parked request. Safe to call when nothing is playing.
    void stop();

private:
    void startPlayback(dpp::discord_voice_client *voiceClient, const std::string &url, const dpp::slashcommand_t &event);
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void pcmResample(dpp::slashcommand_t event);
    void stopSendingData();

    std::atomic<bool> isPlaying{false};

    std::thread audioThread;
    std::thread resamplingThread;

    std::string requestedUrl;
    std::queue<std::vector<uint8_t>> audioQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;

    // A /play issued while the voice connection was still being established.
    std::mutex pendingMutex;
    std::unique_ptr<dpp::slashcommand_t> pendingEvent;
    std::string pendingUrl;
    uint64_t pendingGuildId{0};
};
