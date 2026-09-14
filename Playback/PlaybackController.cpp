#include "PlaybackController.h"
#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Labels.h"
#include "PcmResampler.h"
#include "ResolverWorker.h"

#include <dpp/dpp.h>
#include <QDebug>

#include <algorithm>

PlaybackController::PlaybackController()
{
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
    stopSendingData();
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
    }
}

size_t PlaybackController::skip(size_t count)
{
    size_t fromQueue = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!songInProgress) {
            return 0;
        }
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

    // Ending the current song is enough - the worker joins its threads,
    // flushes dpp's buffer and advances to the next queued song on its own.
    stopSendingData();
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
        // The bot may well still sit in the channel - the idle clock starts.
        idleSinceSeconds = static_cast<int64_t>(time(nullptr));
    }

    // Ends the current song; the worker flushes dpp's buffer itself once
    // the sender thread is gone, so nothing here touches the voice client.
    stopSendingData();

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

int PlaybackController::forward(int seconds)
{
    // Only asks "is a song in progress?" - the client itself stays untouched.
    if (clientIfSongInProgress() == nullptr) {
        return -1;
    }
    if (seconds <= 0) {
        return 0;
    }

    // The decoder runs ahead of playback, so a jump is just discarding PCM
    // from our own queue. dpp's ~1s send buffer stays untouched on purpose:
    // flushing it means calling the voice client while the sender thread is
    // live on it, and dpp's send path has no lock against that.
    const size_t BYTES_PER_SECOND = 192000; // 48kHz * 2ch * 2 bytes
    const size_t bytesToDrop = static_cast<size_t>(seconds) * BYTES_PER_SECOND;
    size_t droppedBytes = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!audioQueue.empty() && droppedBytes < bytesToDrop) {
            droppedBytes += audioQueue.front().size();
            audioQueue.pop();
        }
    }

    if (droppedBytes == 0) {
        return 0; // decoder hasn't buffered anything to skip yet
    }

    // Nearest second, but a real jump never reports as 0 - the reply would
    // claim nothing happened.
    int skipped = static_cast<int>((droppedBytes + BYTES_PER_SECOND / 2) / BYTES_PER_SECOND);
    return std::max(skipped, 1);
}

void PlaybackController::setLoopMode(LoopMode mode)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    loopMode = mode;
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
    while (running) {
        Song song;
        dpp::discord_voice_client *voiceClient = nullptr;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            stateCv.wait(lock, [this] {
                return !running || (!songQueue.empty() && currentVoiceClient != nullptr);
            });
            if (!running) {
                break;
            }
            song = std::move(songQueue.front());
            songQueue.pop_front();
            voiceClient = currentVoiceClient;
            songInProgress = true;
            idleSinceSeconds = 0;
            currentSongLabel = labels::render(song.title, song.webpageUrl, song.target);
        }

        // Blocks until the song ends naturally or is skipped/stopped;
        // finishing this call IS the auto-advance to the next loop turn.
        playSong(voiceClient, song);

        {
            std::lock_guard<std::mutex> lock(stateMutex);

            // If pcmResample re-resolved the song (expired URL), the replay
            // adopts the fresh data - infinite loops stay gapless.
            if (lastResolvedAtSeconds > song.resolvedAtSeconds) {
                song.title = lastResolvedTitle;
                song.webpageUrl = lastResolvedWebpageUrl;
                song.directUrl = lastResolvedDirectUrl;
                song.resolvedAtSeconds = lastResolvedAtSeconds;
            }

            bool endedNaturally = !skipRequested && !currentSongFailed;
            bool sessionAlive = running && currentVoiceClient != nullptr;
            if (sessionAlive && loopMode == LoopMode::Song && endedNaturally) {
                // Repeat-one: back to the front, quietly.
                songQueue.push_front(makeReplay(song, true));
            } else if (sessionAlive && loopMode == LoopMode::Queue && !currentSongFailed) {
                // Repeat-all: rotate to the back (skips stay in the rotation).
                songQueue.push_back(makeReplay(song, false));
            }
            skipRequested = false;

            songInProgress = false;
            currentSongLabel.clear();
            if (songQueue.empty()) {
                idleSinceSeconds = static_cast<int64_t>(time(nullptr));
            }
        }
        // stop() may be waiting for the song threads to be fully joined.
        stateCv.notify_all();
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

void PlaybackController::playSong(dpp::discord_voice_client *voiceClient, const Song &song)
{
    // Shutdown may have happened between popping the song and getting here;
    // re-arming isPlaying now would make the destructor wait out the song.
    if (!running) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = false;
        audioQueue = {};
    }
    requestedUrl = song.target;

    // Hand the resolver's work to pcmResample when it's still fresh enough;
    // googlevideo urls are ip-bound and expire after a few hours.
    const int64_t FRESH_FOR_SECONDS = 3600;
    bool prefetchIsFresh = !song.directUrl.empty()
        && (static_cast<int64_t>(time(nullptr)) - song.resolvedAtSeconds) < FRESH_FOR_SECONDS;
    prefetchedTitle = prefetchIsFresh ? song.title : std::string{};
    prefetchedWebpageUrl = prefetchIsFresh ? song.webpageUrl : std::string{};
    prefetchedDirectUrl = prefetchIsFresh ? song.directUrl : std::string{};
    currentSongWasQueued = song.wasQueued;
    currentSongIsLoopReplay = song.isLoopReplay;
    currentSongFailed = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastResolvedAtSeconds = 0;
    }

    isPlaying = true;

    decoderThread = std::thread(&PlaybackController::pcmResample, this, *song.event);
    senderThread = std::thread(&PlaybackController::streamAudio, this, voiceClient);

    // Sender first: once it is joined, this thread is the only one on the
    // voice client, so this is the one place that may call it after a song.
    // It has to happen before the decoder join - a decoder stuck in yt-dlp
    // can outlive stop()'s bounded wait, and past that the client may be gone.
    senderThread.join();

    // Leftover audio means the song was cut short (skip/stop): flush dpp's
    // ~1s buffer so the next song doesn't queue up behind this one's tail.
    // A song that ended on its own keeps its tail. A pause dies with its song.
    bool cutShort = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        cutShort = !(decodingFinished && audioQueue.empty());
    }
    if (cutShort) {
        voiceClient->stop_audio();
    }
    if (voiceClient->is_paused()) {
        voiceClient->pause_audio(false);
    }

    decoderThread.join();
}

void PlaybackController::stopSendingData()
{
    // isPlaying participates in queueCv wait predicates - flipping it while
    // holding the mutex guarantees no waiter can miss the wakeup.
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        isPlaying = false;
    }
    queueCv.notify_all();
}

void PlaybackController::streamAudio(dpp::discord_voice_client *voiceClient)
{
    // Everything handed to DPP is opus-encoded and (with DAVE E2EE, the
    // default) encrypted with the *current* group key immediately. The key
    // rotates whenever someone joins or leaves, turning any large queued
    // backlog into silence for the listeners - so keep DPP's queue short
    // and hold the deep buffer here as PCM, which no rekey can spoil.
    const float MAX_BUFFERED_SECONDS = 1.0f;

    // The decoder fills the queue with ready-to-send packets of exactly
    // dpp::send_audio_raw_max_length bytes; only the final one may be shorter.
    while (isPlaying) {
        std::vector<uint8_t> packet;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] {
                return !audioQueue.empty() || decodingFinished || !isPlaying;
            });
            if (audioQueue.empty()) {
                if (decodingFinished) break;
                continue;
            }
            packet = std::move(audioQueue.front());
            audioQueue.pop();
        }

        // Never call into dpp while holding queueMutex - a foreign lock
        // inside our critical section is how the whole pipeline wedges.
        while (isPlaying && voiceClient->get_secs_remaining() > MAX_BUFFERED_SECONDS) {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !isPlaying;
            });
        }
        if (!isPlaying) break;

        voiceClient->send_audio_raw((uint16_t*)packet.data(), packet.size());
    }

    isPlaying = false;
}

void PlaybackController::pcmResample(dpp::slashcommand_t event)
{
    struct FinishGuard { std::function<void()> done; ~FinishGuard() { if (done) done(); } };
    FinishGuard finish{[this] {
        { std::lock_guard<std::mutex> lock(queueMutex); decodingFinished = true; }
        queueCv.notify_one();
    }};

    // A queued song announces itself in a fresh channel message, leaving its
    // "Queued at position N" reply intact as history (also immune to the
    // 15-minute interaction token limit). An immediate song still morphs its
    // "Looking for your song..." placeholder.
    const bool announceInNewMessage = currentSongWasQueued;
    auto notifyUser = [&event, announceInNewMessage](dpp::message msg) {
        if (announceInNewMessage) {
            msg.channel_id = event.command.channel_id;
            event.owner->message_create(msg);
        } else {
            event.edit_original_response(msg);
        }
    };

    ResolvedMedia media;
    if (!prefetchedDirectUrl.empty()) {
        // The resolver thread already did the yt-dlp work while the previous
        // song was playing - start immediately.
        media.title = prefetchedTitle;
        media.webpageUrl = prefetchedWebpageUrl;
        media.directUrl = prefetchedDirectUrl;
    } else {
        media = WindowsProcessRunner::resolveMedia(requestedUrl);
        if (!media.directUrl.empty()) {
            // Loop replays adopt this fresh resolution - an infinitely
            // looping song survives its URL expiring without a gap.
            std::lock_guard<std::mutex> lock(stateMutex);
            lastResolvedTitle = media.title;
            lastResolvedWebpageUrl = media.webpageUrl;
            lastResolvedDirectUrl = media.directUrl;
            lastResolvedAtSeconds = static_cast<int64_t>(time(nullptr));
        }
    }

    if (media.directUrl.empty()) {
        currentSongFailed = true;
        notifyUser(dpp::message(messages::errorResolve));
        return;
    }

    // A skip/stop can land while yt-dlp runs - don't open a stream or
    // announce a song nobody wants anymore.
    if (!isPlaying) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentSongLabel = labels::render(media.title, media.webpageUrl, requestedUrl);
    }

    auto fail = [&](const char *msg) {
        currentSongFailed = true;
        notifyUser(dpp::message(msg));
    };

    PcmResampler resampler(dpp::send_audio_raw_max_length);
    switch (resampler.open(media.directUrl)) {
        case PcmResampler::Result::OpenFailed: fail(messages::errorOpenStream); return;
        case PcmResampler::Result::ReadFailed: fail(messages::errorReadStream); return;
        case PcmResampler::Result::NoAudio: fail(messages::errorNoAudio); return;
        case PcmResampler::Result::Ok: break;
    }

    if (!isPlaying) return;

    // The title is untrusted input from the video page - disable every kind
    // of mention so a title like "@everyone" can't ping the server. Rendered
    // as a masked link: clickable title, no embed preview. Song-mode loop
    // replays stay quiet - nobody needs the same title announced 20 times.
    if (!currentSongIsLoopReplay) {
        dpp::message nowPlaying(messages::playingPrefix + labels::render(media.title, media.webpageUrl, requestedUrl));
        nowPlaying.set_allowed_mentions();
        notifyUser(nowPlaying);
    }

    resampler.run(
        [this](std::vector<uint8_t> pkt) {
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                audioQueue.push(std::move(pkt));
            }
            queueCv.notify_one();
        },
        [this] { return isPlaying.load(); });
}
