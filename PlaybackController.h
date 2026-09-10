#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
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

// Owns the whole playback pipeline: the song queue, yt-dlp resolution,
// FFmpeg decode/resample, and the paced hand-off to DPP. A persistent
// worker thread plays queued songs one after another; commands stay thin
// and only talk to this interface.
class PlaybackController
{
public:
    struct QueueSnapshot
    {
        std::string current;              // title (or url) of the playing song, empty when idle
        std::vector<std::string> queued;  // urls waiting in the queue
    };

    PlaybackController();
    ~PlaybackController();

    // Enqueues the given YouTube url. Returns 0 when the song will start
    // right away, otherwise its 1-based position among the waiting songs.
    // Songs wait until a voice connection is available; onVoiceReady
    // releases them once the handshake completes.
    size_t play(const std::string &youtubeUrl, const dpp::slashcommand_t &event);

    // Skips the currently playing song; the worker advances to the next
    // queued one. Returns false when nothing was playing.
    bool skip();

    // Stops the current song and clears the whole queue. Safe to call when
    // nothing is playing. The worker thread stays alive for the next /play.
    void stop();

    // Called by the bot's on_voice_ready handler once a voice connection
    // can accept audio.
    void onVoiceReady(const dpp::voice_ready_t &event);

    QueueSnapshot queueSnapshot();

private:
    struct Song
    {
        std::string youtubeUrl;
        std::unique_ptr<dpp::slashcommand_t> event;
    };

    void playbackWorker();
    void playSong(dpp::discord_voice_client *voiceClient, Song song);
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void pcmResample(dpp::slashcommand_t event);
    void stopSendingData();

    // Queue + worker state, guarded by stateMutex.
    std::mutex stateMutex;
    std::condition_variable stateCv;
    std::deque<Song> songQueue;
    dpp::discord_voice_client *currentVoiceClient{nullptr};
    bool songActive{false};
    std::string currentSongLabel; // url, replaced by the title once resolved
    std::atomic<bool> running{true};
    std::thread workerThread;

    // Per-song pipeline state.
    std::atomic<bool> isPlaying{false};
    std::thread audioThread;
    std::thread resamplingThread;
    std::string requestedUrl;
    std::queue<std::vector<uint8_t>> audioQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;
};
