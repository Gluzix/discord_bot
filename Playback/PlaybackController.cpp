#include "PlaybackController.h"
#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Labels.h"
#include "SongPlayer.h"
#include "ResolverWorker.h"
#include "Log.h"

#include <dpp/dpp.h>
#include <QDebug>

#include <algorithm>
#include <chrono>

PlaybackController::PlaybackController()
{
    songPlayer = std::make_unique<SongPlayer>([this](std::string label) {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentSongLabel = std::move(label);
    });
    // ResolverWorker starts and owns its own thread.
    resolverWorker = std::make_unique<ResolverWorker>(songQueue, stateCv, stateMutex);
    workerThread = std::thread(&PlaybackController::playbackWorker, this);
}

PlaybackController::~PlaybackController()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        running = false;
        songQueue.clear();
    }
    songPlayer->stop();
    stateCv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
    // Stops and joins the resolver's thread. The queue/cv/mutex it borrows
    // are controller members, still alive at this point.
    resolverWorker.reset();
}

size_t PlaybackController::play(const std::string &youtubeUrl, const dpp::slashcommand_t &event)
{
    size_t waitingPosition = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);

        bool busy = songInProgress || !songQueue.empty();

        Song song;
        song.id = nextSongId++;
        song.target = youtubeUrl;
        song.wasQueued = busy;
        song.event = std::make_unique<dpp::slashcommand_t>(event);
        songQueue.push_back(std::move(song));
        noteRequest(event);

        if (busy) {
            waitingPosition = songQueue.size();
        }
    }
    stateCv.notify_all();
    return waitingPosition;
}

size_t PlaybackController::playPlaylist(const std::vector<PlaylistEntry> &entries, const dpp::slashcommand_t &event)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (const PlaylistEntry &entry : entries) {
            Song song;
            song.id = nextSongId++;
            song.target = entry.webpageUrl;
            song.title = entry.title; // /queue shows real titles right away
            song.webpageUrl = entry.webpageUrl;
            song.wasQueued = true;    // every "Playing:" goes out as a fresh message
            song.fromPlaylist = true;
            song.event = std::make_unique<dpp::slashcommand_t>(event);
            songQueue.push_back(std::move(song));
        }
        noteRequest(event);
    }
    stateCv.notify_all();
    return entries.size();
}

// The idle clock stops, farewells know where to go, and a ready voice
// connection travels with the request. If the handshake is still in flight,
// songs simply wait in the queue until onVoiceReady provides the client.
void PlaybackController::noteRequest(const dpp::slashcommand_t &event)
{
    idleSinceSeconds = 0;
    lastTextChannelId = event.command.channel_id;

    dpp::voiceconn* vc = event.from()->get_voice(event.command.guild_id);
    if (vc && vc->voiceclient && vc->voiceclient->is_ready()) {
        currentVoiceClient = vc->voiceclient.get();
        // The session state must be whole again after a /stop zeroed it.
        activeChannelId = static_cast<uint64_t>(currentVoiceClient->channel_id);
        activeGuildId = static_cast<uint64_t>(currentVoiceClient->server_id);
    }
}

bool PlaybackController::isSongPlaying()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return songInProgress;
}

size_t PlaybackController::skip(size_t count)
{
    size_t fromQueue = 0;
    size_t heldSkipped = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!songInProgress) {
            // A held song at the front is the current one: skipping it drops
            // it and lifts the wait. The streak goes on while songs remain.
            const bool holding = !songQueue.empty() && songQueue.front().failedAttempts > 0;
            if (!holding) {
                return 0;
            }
            heldSkipped = std::min(std::max<size_t>(count, 1), songQueue.size());
            songQueue.front().failedAttempts = 0; // its next turn starts clean
            if (loopMode == LoopMode::Queue) {
                std::rotate(songQueue.begin(), songQueue.begin() + heldSkipped, songQueue.end());
            } else {
                songQueue.erase(songQueue.begin(), songQueue.begin() + heldSkipped);
            }
            retryNotBefore = {};
            if (songQueue.empty()) {
                // Nothing left, as after a last song: the idle clock starts
                // and the streak is over.
                idleSinceSeconds = static_cast<int64_t>(time(nullptr));
                failureStreak.reset();
            }
        } else {
            // The current song counts as one; the rest come off the front of
            // the queue. Queue-loop mode keeps them in the rotation.
            fromQueue = std::min(count > 0 ? count - 1 : 0, songQueue.size());
            if (loopMode == LoopMode::Queue) {
                std::rotate(songQueue.begin(), songQueue.begin() + fromQueue, songQueue.end());
            } else {
                songQueue.erase(songQueue.begin(), songQueue.begin() + fromQueue);
            }
            // Song-mode looping must not resurrect a song the user just skipped.
            skipRequested = true;
        }
    }

    if (heldSkipped > 0) {
        // Nothing to stop - only the worker's wait to cut short.
        stateCv.notify_all();
        return heldSkipped;
    }

    // Ending the current song is enough - the worker advances to the next
    // queued song on its own.
    songPlayer->stop();
    return 1 + fromQueue;
}

void PlaybackController::stop()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        songQueue.clear();
        // The pointer dies with the voice connection on /leave; drop it so
        // the worker can't start a queued song on a dead client. The next
        // /play or onVoiceReady provides a fresh one.
        currentVoiceClient = nullptr;
        activeChannelId = 0;
        loopMode = LoopMode::Off;
        skipRequested = false;
        retryNotBefore = {};
        failureStreak.reset();
        // The bot may well still sit in the channel - the idle clock starts.
        idleSinceSeconds = static_cast<int64_t>(time(nullptr));
    }

    // Ends the current song; the player flushes dpp's buffer itself once
    // its sender thread is gone, so nothing here touches the voice client.
    songPlayer->stop();

    // Wait (bounded) until the worker has joined the song threads, so a
    // caller about to switch channels can safely let dpp destroy the old
    // voice client - no thread of ours may still be touching it.
    {
        std::unique_lock<std::mutex> lock(stateMutex);
        stateCv.wait_for(lock, std::chrono::seconds(2), [this] {
            return !songInProgress;
        });
    }
}

dpp::discord_voice_client* PlaybackController::clientIfSongInProgress()
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return songInProgress ? currentVoiceClient : nullptr;
}

bool PlaybackController::pause()
{
    dpp::discord_voice_client *voiceClient = clientIfSongInProgress();
    if (voiceClient && !voiceClient->is_paused()) {
        voiceClient->pause_audio(true);
        return true;
    }

    return false;
}

bool PlaybackController::resume()
{
    dpp::discord_voice_client *voiceClient = clientIfSongInProgress();
    if (voiceClient && voiceClient->is_paused()) {
        voiceClient->pause_audio(false);
        return true;
    }

    return false;
}

namespace {

// Reached only with a song in progress, so playing is true either way.
PlaybackController::SeekResult toSeekResult(const std::optional<SongPlayer::Position> &position)
{
    PlaybackController::SeekResult result;
    result.playing = true;
    if (position) {
        result.seekable = true;
        result.positionSeconds = position->seconds;
        result.durationSeconds = position->durationSeconds;
    }
    return result;
}

}

PlaybackController::SeekResult PlaybackController::seekBy(int deltaSeconds)
{
    // Only asks "is a song in progress?" - the jump happens in the player.
    if (!isSongPlaying()) {
        return SeekResult{};
    }
    return toSeekResult(songPlayer->seekBy(deltaSeconds));
}

PlaybackController::SeekResult PlaybackController::seekTo(int positionSeconds)
{
    if (!isSongPlaying()) {
        return SeekResult{};
    }
    return toSeekResult(songPlayer->seekTo(positionSeconds));
}

void PlaybackController::setLoopMode(LoopMode mode)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    loopMode = mode;
}

void PlaybackController::setVoiceLostHandler(std::function<void(uint64_t, uint64_t)> handler)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    voiceLostHandler = std::move(handler);
}

void PlaybackController::setVoiceClientLookup(VoiceClientLookup lookup)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    voiceClientLookup = std::move(lookup);
}

void PlaybackController::onVoiceReady(const dpp::voice_ready_t &event)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentVoiceClient = event.voice_client;
        activeChannelId = event.voice_client ? static_cast<uint64_t>(event.voice_client->channel_id) : 0;
        if (event.voice_client) {
            activeGuildId = static_cast<uint64_t>(event.voice_client->server_id);
        }
        // Joined but with nothing to play - the idle clock starts.
        if (!songInProgress && songQueue.empty()) {
            idleSinceSeconds = static_cast<int64_t>(time(nullptr));
        }
    }
    stateCv.notify_all();
}

void PlaybackController::onBotVoiceStateChanged(uint64_t channelId)
{
    bool movedAway = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        movedAway = activeChannelId != 0 && channelId != activeChannelId;
    }
    if (movedAway) {
        stop();
    }
}

PlaybackController::IdleInfo PlaybackController::idleInfo()
{
    IdleInfo info;
    std::lock_guard<std::mutex> lock(stateMutex);
    info.idle = !songInProgress && songQueue.empty();
    info.idleSinceSeconds = idleSinceSeconds;
    info.guildId = activeGuildId;
    info.textChannelId = lastTextChannelId;
    return info;
}

PlaybackController::SessionInfo PlaybackController::sessionInfo()
{
    SessionInfo info;
    std::lock_guard<std::mutex> lock(stateMutex);
    info.active = songInProgress || !songQueue.empty();
    info.channelId = activeChannelId;
    return info;
}

PlaybackController::QueueSnapshot PlaybackController::queueSnapshot()
{
    QueueSnapshot snapshot;
    std::lock_guard<std::mutex> lock(stateMutex);
    snapshot.current = currentSongLabel;
    snapshot.loop = loopMode;
    for (const Song &song : songQueue) {
        snapshot.queued.push_back(labels::render(song.title, song.webpageUrl, song.target));
    }
    return snapshot;
}

void PlaybackController::playbackWorker()
{
    logging::nameThisThread("worker");

    // Long enough for a network outage to pass, short enough that the song
    // comes back on its own.
    const std::chrono::seconds RETRY_AFTER(30);

    while (running) {
        Song song;
        dpp::discord_voice_client *voiceClient = nullptr;
        // Taken while the client is known-good; after a lost session it may
        // already be destroyed, and these are the ids a rejoin has to use.
        uint64_t songChannelId = 0;
        uint64_t songGuildId = 0;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            // A front song the resolver is still on is left to it - popping
            // it now would only resolve it a second time.
            auto songReady = [this] {
                return !songQueue.empty() && currentVoiceClient != nullptr
                       && !songQueue.front().resolveInFlight;
            };
            // Every wake re-checks running, so shutdown still gets out at once.
            while (running) {
                if (!songReady()) {
                    stateCv.wait(lock);
                } else if (std::chrono::steady_clock::now() < retryNotBefore) {
                    stateCv.wait_until(lock, retryNotBefore);
                } else {
                    break;
                }
            }
            if (!running) {
                break;
            }
            // dpp may have destroyed and replaced the client while nothing
            // was playing - only the shard knows which one is live now.
            if (voiceClientLookup) {
                dpp::discord_voice_client *live = voiceClientLookup(activeGuildId);
                if (live != currentVoiceClient) {
                    currentVoiceClient = live;
                    if (live == nullptr) {
                        qWarning() << "Voice client is gone - holding the queue until a new session is ready";
                        continue; // back to waiting; onVoiceReady provides the next client
                    }
                    qWarning() << "Voice client was replaced under us - adopting the live one";
                    activeChannelId = static_cast<uint64_t>(live->channel_id);
                }
            }
            song = std::move(songQueue.front());
            songQueue.pop_front();
            voiceClient = currentVoiceClient;
            songChannelId = static_cast<uint64_t>(voiceClient->channel_id);
            songGuildId = static_cast<uint64_t>(voiceClient->server_id);
            songInProgress = true;
            // Armed in the same critical section, so a skip/stop that sees the
            // song as in progress always reaches it - even before play().
            songPlayer->arm();
            idleSinceSeconds = 0;
            currentSongLabel = labels::render(song.title, song.webpageUrl, song.target);
        }

        // Shutdown can begin between the pop above and here; don't start a
        // new song then, but still run the cleanup below so songInProgress
        // clears. play() updates song's resolved fields in place if it
        // re-resolved, so a loop replay picks up the fresh url for free.
        SongPlayer::Outcome outcome = running ? songPlayer->play(voiceClient, song)
                                              : SongPlayer::Outcome::Failed;
        bool ok = outcome == SongPlayer::Outcome::Finished;

        std::function<void(uint64_t, uint64_t)> reportVoiceLost;
        uint64_t lostGuildId = 0;
        uint64_t lostChannelId = 0;
        bool announceHold = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);

            bool endedNaturally = !skipRequested && ok;
            bool sessionAlive = running && currentVoiceClient != nullptr;
            // A skip can land right after the decoder gave up, and a skipped
            // song is never held.
            const bool failed = outcome == SongPlayer::Outcome::Failed && !skipRequested;
            if (!failed) {
                failureStreak.reset();
            }

            if (outcome == SongPlayer::Outcome::VoiceLost) {
                if (sessionAlive) {
                    // Back to the front, announced again when it resumes.
                    songQueue.push_front(makeReplay(song, false));
                }
                // dpp's own full reconnection may have handed us a new client
                // meanwhile - that one works, don't throw it away.
                if (sessionAlive && currentVoiceClient == voiceClient) {
                    currentVoiceClient = nullptr;
                    reportVoiceLost = voiceLostHandler;
                    lostGuildId = songGuildId;
                    lostChannelId = songChannelId;
                }
            } else if (failed && sessionAlive) {
                const std::string label = song.title.empty() ? song.target : song.title;
                const FailureStreak::Decision decision = failureStreak.recordFailure(song.failedAttempts);
                if (decision.retry) {
                    Song retry = makeReplay(song, false);
                    retry.failedAttempts = song.failedAttempts + 1;
                    retry.directUrl.clear(); // the url may be what failed
                    retry.resolvedAtSeconds = 0;
                    qWarning().noquote() << "Failure" << failureStreak.length() << "in a row - holding"
                                         << QString::fromStdString(label) << "for retry"
                                         << retry.failedAttempts << "of" << MAX_RETRIES_PER_SONG;
                    songQueue.push_front(std::move(retry));
                    retryNotBefore = std::chrono::steady_clock::now() + RETRY_AFTER;
                    announceHold = decision.firstOfStreak;
                } else if (song.failedAttempts > 0) {
                    qWarning().noquote() << "Giving up on" << QString::fromStdString(label) << "after"
                                         << song.failedAttempts << "retries";
                } else {
                    qWarning().noquote() << "Dropping" << QString::fromStdString(label) << "- it failed";
                }
            } else if (sessionAlive && loopMode == LoopMode::Song && endedNaturally) {
                // Repeat-one: back to the front, quietly.
                songQueue.push_front(makeReplay(song, true));
            } else if (sessionAlive && loopMode == LoopMode::Queue && ok) {
                // Repeat-all: rotate to the back (skips stay in the rotation).
                songQueue.push_back(makeReplay(song, false));
            }
            skipRequested = false;

            songInProgress = false;
            currentSongLabel.clear();
            if (songQueue.empty()) {
                idleSinceSeconds = static_cast<int64_t>(time(nullptr));
                failureStreak.reset(); // failures far apart must not add up
            }
        }
        // stop() may be waiting for the song threads to be fully joined.
        stateCv.notify_all();

        // Never under stateMutex. The local song still owns its event - the
        // retry pushed above got a copy.
        if (announceHold) {
            dpp::message notice(messages::retryingSong);
            notice.channel_id = song.event->command.channel_id;
            song.event->owner->message_create(notice);
        }

        if (reportVoiceLost) {
            reportVoiceLost(lostGuildId, lostChannelId);
        }
    }
}

Song PlaybackController::makeReplay(const Song &song, bool loopReplay)
{
    Song replay;
    replay.id = nextSongId++;
    replay.target = song.target;
    replay.wasQueued = true; // any announcement goes out as a fresh message
    replay.event = std::make_unique<dpp::slashcommand_t>(*song.event);
    replay.title = song.title;
    replay.webpageUrl = song.webpageUrl;
    replay.directUrl = song.directUrl;
    replay.resolvedAtSeconds = song.resolvedAtSeconds;
    replay.isLoopReplay = loopReplay;
    replay.fromPlaylist = song.fromPlaylist;
    return replay;
}
