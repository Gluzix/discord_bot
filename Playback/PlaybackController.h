#pragma once

#include "FailureStreak.h"
#include "Song.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dpp {
class discord_voice_client;
struct voice_ready_t;
struct slashcommand_t;
}

class ResolverWorker;
class SongPlayer;

struct PlaylistEntry; // WindowsProcessRunner.h

// Owns the song queue, the loop policy and the voice session.
// =======================================================
// Rules:
// - No dpp call under stateMutex. The voice-client lookup is the one
//   exception: it runs under it, may look things up in dpp, and must not
//   call back into this controller.
// - skip() and stop() only end the song and never touch the voice client:
//   SongPlayer::play() flushes it once its sender is joined.
// - songPlayer->arm() goes in the critical section that pops the song and
//   sets songInProgress, so a skip/stop that sees the song as in progress
//   always reaches it - even before play().
// - stop() waits, bounded, until the worker has joined the song threads, so
//   a caller about to switch channels can let dpp destroy the old voice
//   client.
// - After a lost session the song's client may already be destroyed: the
//   rejoin uses the ids taken at the pop, while it was known-good.
// - Whenever a song ends with nothing left to play, the idle clock starts and
//   the failure streak ends: with the clock at 0 the idle timer never leaves
//   the channel.
// =======================================================
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
        std::vector<std::string> queued;  // titles (or urls) of the waiting songs
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

    bool isPaused();

    // Persists until changed or /stop turns it Off.
    void setLoopMode(LoopMode mode);

    struct SeekResult
    {
        bool playing{false};       // a song is in progress
        bool seekable{false};      // ... and the jump landed, so the fields below hold
        double positionSeconds{0};
        double durationSeconds{0}; // 0 = unknown
    };

    // Jumps in the current song, by `deltaSeconds` or to an absolute
    // position, and reports where playback landed.
    SeekResult seekBy(int deltaSeconds);
    SeekResult seekTo(int positionSeconds);

    // Called (from the worker thread) when the voice client stopped taking
    // audio: the session is dropped and the song put back, so the handler
    // only has to get a working connection. Set it before playback starts.
    void setVoiceLostHandler(std::function<void(uint64_t guildId, uint64_t channelId)> handler);

    // Asked before every song which voice client the shard holds for the
    // guild right now (nullptr when none is ready): dpp may have replaced
    // ours while nothing was playing. Set it before playback starts.
    using VoiceClientLookup = std::function<dpp::discord_voice_client *(uint64_t guildId)>;
    void setVoiceClientLookup(VoiceClientLookup lookup);

    // Called by the bot's on_voice_ready handler once a voice connection
    // can accept audio.
    void onVoiceReady(const dpp::voice_ready_t &event);

    // Called with the bot's own voice channel id whenever its voice state
    // changes; a move to another channel (or out of voice) destroys the old
    // client, so playback stops.
    void onBotVoiceStateChanged(uint64_t channelId);

    QueueSnapshot queueSnapshot();

private:


    // Call with stateMutex held.
    void noteRequest(const dpp::slashcommand_t &event);
    bool isSongPlaying();

    // One locked snapshot: the current song's voice client, or nullptr when
    // no song is in progress. Callers work with the returned copy only.
    dpp::discord_voice_client* clientIfSongInProgress();

    void playbackWorker();
    Song makeReplay(const Song &song, bool loopReplay); // call with stateMutex held (uses nextSongId)

    // Queue + worker state, guarded by stateMutex.
    std::mutex stateMutex;
    std::condition_variable stateCv;
    std::deque<Song> songQueue;
    dpp::discord_voice_client *currentVoiceClient{nullptr};
    uint64_t activeChannelId{0};
    uint64_t activeGuildId{0};      // last known guild, survives stop/leave
    int64_t idleSinceSeconds{0};    // set when playback goes quiet
    uint64_t lastTextChannelId{0};  // farewell messages go here
    // The worker is inside SongPlayer::play. Stays true while paused -
    // it tracks the song's lifecycle, not whether audio is audible.
    bool songInProgress{false};
    LoopMode loopMode{LoopMode::Off};
    bool skipRequested{false}; // song-mode: a skipped song must not requeue itself
    std::string currentSongLabel; // pre-rendered markdown for /queue
    // A lone failure is a broken song; failures back to back are a broken
    // network, and then the song is worth keeping.
    static constexpr int HOLD_FROM_FAILURE = 2;
    static constexpr int MAX_RETRIES_PER_SONG = 5;
    FailureStreak failureStreak{HOLD_FROM_FAILURE, MAX_RETRIES_PER_SONG};
    std::chrono::steady_clock::time_point retryNotBefore{}; // the epoch means no song is held
    std::function<void(uint64_t, uint64_t)> voiceLostHandler;
    VoiceClientLookup voiceClientLookup;
    uint64_t nextSongId{1};
    std::atomic<bool> running{true};
    std::thread workerThread;

    std::unique_ptr<SongPlayer> songPlayer;
    std::unique_ptr<ResolverWorker> resolverWorker;
};
