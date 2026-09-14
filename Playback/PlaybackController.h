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

struct PlaylistEntry; // WindowsProcessRunner.h

// Owns the whole playback pipeline: the song queue, yt-dlp resolution,
// FFmpeg decode/resample, and the paced hand-off to DPP. A persistent
// worker thread plays queued songs one after another; commands stay thin
// and only talk to this interface.
class PlaybackController
{
public:
    enum class LoopMode {
        Off,
        Song,  // repeat the current song when it ends naturally
        Queue, // finished (or skipped) songs rotate to the back of the queue
    };

    struct QueueSnapshot
    {
        std::string current;              // title (or url) of the playing song, empty when idle
        std::vector<std::string> queued;  // urls waiting in the queue
        LoopMode loop{LoopMode::Off};
    };

    struct SessionInfo
    {
        bool active{false};     // a song is playing or waiting in the queue
        uint64_t channelId{0};  // the voice channel the session lives in (0 if unknown yet)
    };

    SessionInfo sessionInfo();

    struct IdleInfo
    {
        bool idle{false};             // nothing playing and the queue is empty
        int64_t idleSinceSeconds{0};  // when the idling started (0 = unknown)
        uint64_t guildId{0};          // last known voice guild (0 = never joined)
        uint64_t textChannelId{0};    // where the last /play came from
    };

    // For the idle-timeout timer: says how long the bot has been useless.
    IdleInfo idleInfo();

    PlaybackController();
    ~PlaybackController();

    // Enqueues the given YouTube url. Returns 0 when the song will start
    // right away, otherwise its 1-based position among the waiting songs.
    // Songs wait until a voice connection is available; onVoiceReady
    // releases them once the handshake completes.
    size_t play(const std::string &youtubeUrl, const dpp::slashcommand_t &event);

    // Enqueues every listed playlist entry, in order, and returns how many.
    // The first starts right away when nothing is playing. All of them
    // announce themselves in fresh messages, so the /playlist reply stays
    // the summary instead of morphing into the first "Playing:".
    size_t playPlaylist(const std::vector<PlaylistEntry> &entries, const dpp::slashcommand_t &event);

    // Skips the current song and, for count > 1, the next count-1 queued
    // ones too (queue-loop mode rotates them to the back instead).
    // Returns how many songs went, 0 when nothing was playing.
    size_t skip(size_t count = 1);

    // Stops the current song and clears the whole queue. Safe to call when
    // nothing is playing. The worker thread stays alive for the next /play.
    void stop();

    bool pause();

    bool resume();

    // Sets the loop mode. Persists until changed or /stop turns it Off.
    void setLoopMode(LoopMode mode);

    // Jumps ahead in the current song by discarding buffered PCM.
    // Returns the whole seconds actually skipped (at least 1 for any real
    // jump), 0 when nothing was buffered yet, or -1 when nothing is playing.
    int forward(int seconds);

    // Called by the bot's on_voice_ready handler once a voice connection
    // can accept audio.
    void onVoiceReady(const dpp::voice_ready_t &event);

    // Called with the bot's own voice channel id whenever its voice state
    // changes; a move to another channel (or out of voice) destroys the old
    // client, so playback stops.
    void onBotVoiceStateChanged(uint64_t channelId);

    QueueSnapshot queueSnapshot();

private:
    struct Song
    {
        uint64_t id{0};
        std::string target; // youtube url or "ytsearch1:<query>"
        bool wasQueued{false}; // waited in the queue vs started right away
        std::unique_ptr<dpp::slashcommand_t> event;

        // Filled by the resolver thread ahead of time; empty until then.
        std::string title;
        std::string webpageUrl;
        std::string directUrl;
        int64_t resolvedAtSeconds{0};
        bool resolveFailed{false}; // resolver gave up; playSong retries itself
        bool isLoopReplay{false};  // song-mode repeat: suppress the "Playing:" announcement
        bool fromPlaylist{false};  // shares the /playlist reply: no per-song queue edits
    };

    // Bookkeeping shared by every enqueue. Call with stateMutex held.
    void noteRequest(const dpp::slashcommand_t &event);

    // One locked snapshot: the current song's voice client, or nullptr when
    // no song is in progress. Callers work with the returned copy only.
    dpp::discord_voice_client* clientIfSongInProgress();

    void playbackWorker();
    void resolverWorker();
    void playSong(dpp::discord_voice_client *voiceClient, const Song &song);
    Song makeReplay(const Song &song, bool loopReplay); // call with stateMutex held (uses nextSongId)
    void streamAudio(dpp::discord_voice_client *voiceClient);
    void pcmResample(dpp::slashcommand_t event);
    void stopSendingData();

    // Queue + worker state, guarded by stateMutex.
    std::mutex stateMutex;
    std::condition_variable stateCv;
    std::deque<Song> songQueue;
    dpp::discord_voice_client *currentVoiceClient{nullptr};
    uint64_t activeChannelId{0};
    uint64_t activeGuildId{0};      // last known guild, survives stop/leave
    int64_t idleSinceSeconds{0};    // set when playback goes quiet
    uint64_t lastTextChannelId{0};  // farewell messages go here
    // The worker is inside playSong for some song. Stays true while paused -
    // it tracks the song's lifecycle, not whether audio is audible.
    bool songInProgress{false};
    LoopMode loopMode{LoopMode::Off};
    bool skipRequested{false}; // song-mode: a skipped song must not requeue itself
    std::string currentSongLabel; // pre-rendered markdown for /queue
    uint64_t nextSongId{1};
    std::atomic<bool> running{true};
    std::thread workerThread;
    std::thread resolverThread;

    // Per-song pipeline state.
    std::atomic<bool> isPlaying{false};
    std::thread senderThread;
    std::thread decoderThread;
    std::string requestedUrl;

    // Hand-off from playSong to pcmResample when the resolver already did
    // the work; empty means pcmResample resolves on its own.
    std::string prefetchedTitle;
    std::string prefetchedWebpageUrl;
    std::string prefetchedDirectUrl;

    // A queued song keeps its "Queued at position N" message intact and
    // announces itself in a fresh message; an immediate song still morphs
    // its "Looking for your song..." placeholder.
    bool currentSongWasQueued{false};
    bool currentSongIsLoopReplay{false}; // suppresses the repeat announcements
    bool currentSongFailed{false};       // failed songs never requeue (no error loops)

    // Fresh resolution done by pcmResample for the current song, adopted by
    // loop replays - keeps an infinite loop gapless after the original URL
    // expires. Guarded by stateMutex.
    std::string lastResolvedTitle;
    std::string lastResolvedWebpageUrl;
    std::string lastResolvedDirectUrl;
    int64_t lastResolvedAtSeconds{0};
    std::queue<std::vector<uint8_t>> audioQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    bool decodingFinished = false;
};
