#include "PlaybackController.h"
#include "WindowsProcessRunner.h"
#include "Messages.h"

#include <dpp/dpp.h>
#include <QDebug>

#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

// Queue entries hold the raw yt-dlp target; show searches in a friendlier
// form until the real title is resolved. <> around a bare url stops Discord
// from unfurling an embed preview for it.
static std::string displayLabelFor(const std::string &target)
{
    const std::string searchPrefix = "ytsearch1:";
    if (target.rfind(searchPrefix, 0) == 0) {
        return messages::searchLabelPrefix + target.substr(searchPrefix.size());
    }
    if (target.rfind("http", 0) == 0) {
        return "<" + target + ">";
    }
    return target;
}

// A resolved song renders as a masked link - the title as clickable text,
// <> suppressing the embed preview. Unresolved songs fall back to the target.
static std::string renderLabel(const std::string &title, const std::string &webpageUrl, const std::string &fallbackTarget)
{
    if (!title.empty() && !webpageUrl.empty()) {
        return "[" + title + "](<" + webpageUrl + ">)";
    }
    if (!title.empty()) {
        return title;
    }
    return displayLabelFor(fallbackTarget);
}

PlaybackController::PlaybackController()
{
    workerThread = std::thread(&PlaybackController::playbackWorker, this);
    resolverThread = std::thread(&PlaybackController::resolverWorker, this);
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
    if (resolverThread.joinable()) {
        resolverThread.join();
    }
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

        idleSinceSeconds = 0;
        lastTextChannelId = event.command.channel_id;

        // A ready voice connection travels with the request; if the
        // handshake is still in flight, songs simply wait in the queue
        // until onVoiceReady provides the client.
        dpp::voiceconn* vc = event.from()->get_voice(event.command.guild_id);
        if (vc && vc->voiceclient && vc->voiceclient->is_ready()) {
            currentVoiceClient = vc->voiceclient.get();
        }

        if (busy) {
            waitingPosition = songQueue.size();
        }
    }
    stateCv.notify_all();
    return waitingPosition;
}

bool PlaybackController::skip()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!songInProgress) {
            return false;
        }
        // Song-mode looping must not resurrect a song the user just skipped.
        skipRequested = true;
    }

    // Ending the current song is enough - the worker joins its threads,
    // flushes dpp's buffer and advances to the next queued song on its own.
    stopSendingData();
    return true;
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
    // live on it - the race that crashed.
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
        snapshot.queued.push_back(renderLabel(song.title, song.webpageUrl, song.target));
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
            currentSongLabel = renderLabel(song.title, song.webpageUrl, song.target);
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

// Resolves queued songs ahead of time: titles show up in /queue and the
// "Queued at position N" replies, and playSong can start a prefetched song
// without the multi-second yt-dlp pause between tracks.
void PlaybackController::resolverWorker()
{
    while (running) {
        uint64_t songId = 0;
        std::string target;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            stateCv.wait(lock, [this] {
                if (!running) {
                    return true;
                }
                for (const Song &song : songQueue) {
                    if (song.directUrl.empty() && !song.resolveFailed) {
                        return true;
                    }
                }
                return false;
            });
            if (!running) {
                break;
            }
            for (const Song &song : songQueue) {
                if (song.directUrl.empty() && !song.resolveFailed) {
                    songId = song.id;
                    target = song.target;
                    break;
                }
            }
        }
        if (songId == 0) {
            continue;
        }

        ResolvedMedia media = WindowsProcessRunner::resolveMedia(target);

        std::unique_ptr<dpp::slashcommand_t> requestEvent;
        std::string label;
        size_t position = 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            for (size_t i = 0; i < songQueue.size(); ++i) {
                Song &song = songQueue[i];
                if (song.id != songId) {
                    continue;
                }
                if (media.directUrl.empty()) {
                    // Give up quietly; playSong retries and reports the
                    // error to the user when the song's turn comes.
                    song.resolveFailed = true;
                } else {
                    song.title = media.title;
                    song.webpageUrl = media.webpageUrl;
                    song.directUrl = media.directUrl;
                    song.resolvedAtSeconds = static_cast<int64_t>(time(nullptr));
                    label = renderLabel(song.title, song.webpageUrl, song.target);
                    position = i + 1;
                    requestEvent = std::make_unique<dpp::slashcommand_t>(*song.event);
                }
                break;
            }
        }

        // Upgrade the "Queued at position N" reply with what we found.
        if (requestEvent) {
            dpp::message queuedInfo(messages::queuedAtPrefix + std::to_string(position) + messages::queuedSeparator + label);
            queuedInfo.set_allowed_mentions();
            requestEvent->edit_original_response(queuedInfo);
        }
    }
}

PlaybackController::Song PlaybackController::makeReplay(const Song &song, bool loopReplay)
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
    // Whatever happens here, the sender thread waits on the queue and must
    // be released - every exit path has to mark decoding as finished.
    auto signalFinished = [this]() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            decodingFinished = true;
        }
        queueCv.notify_one();
    };

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
        signalFinished();
        return;
    }

    // A skip/stop can land while yt-dlp runs - don't open a stream or
    // announce a song nobody wants anymore.
    if (!isPlaying) {
        signalFinished();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        currentSongLabel = renderLabel(media.title, media.webpageUrl, requestedUrl);
    }

    std::string directUrl = media.directUrl;

    AVDictionary *options = nullptr;
    av_dict_set(&options, "headers",
                "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n",
                0);
    av_dict_set(&options, "buffer_size", "1048576", 0); // 1MB read buffer
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "multiple_requests", "1", 0);
    AVFormatContext *format = nullptr;
    int errorCode = avformat_open_input(&format, directUrl.c_str(), nullptr, &options);
    av_dict_free(&options); // open_input consumed what it needed

    if (errorCode != 0) {
        char errbuf[256];
        av_strerror(errorCode, errbuf, sizeof(errbuf));
        qDebug() << "Cannot open input! avformat_open_input returned with " << errorCode << errbuf;
        currentSongFailed = true;
        notifyUser(dpp::message(messages::errorOpenStream));
        signalFinished();
        return;
    }

    errorCode = avformat_find_stream_info(format, nullptr);
    if (errorCode != 0) {
        qDebug() << "Cannot find stream info! avformat_find_stream_info returned with " << errorCode;
        avformat_close_input(&format);
        currentSongFailed = true;
        notifyUser(dpp::message(messages::errorReadStream));
        signalFinished();
        return;
    }

    int audioStream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioStream < 0) {
        qDebug() << "Couldn't find audio stream! av_find_best_stream returned with " << audioStream;
        avformat_close_input(&format);
        currentSongFailed = true;
        notifyUser(dpp::message(messages::errorNoAudio));
        signalFinished();
        return;
    }

    // Same check after the (slow) network open.
    if (!isPlaying) {
        avformat_close_input(&format);
        signalFinished();
        return;
    }

    // The title is untrusted input from the video page - disable every kind
    // of mention so a title like "@everyone" can't ping the server. Rendered
    // as a masked link: clickable title, no embed preview. Song-mode loop
    // replays stay quiet - nobody needs the same title announced 20 times.
    if (!currentSongIsLoopReplay) {
        dpp::message nowPlaying(messages::playingPrefix + renderLabel(media.title, media.webpageUrl, requestedUrl));
        nowPlaying.set_allowed_mentions();
        notifyUser(nowPlaying);
    }

    const AVCodec *codec = avcodec_find_decoder(format->streams[audioStream]->codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codec_ctx, format->streams[audioStream]->codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    SwrContext* swr = nullptr;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout in_ch_layout = codec_ctx->ch_layout;

    swr_alloc_set_opts2(&swr, &out_ch_layout,
                        AV_SAMPLE_FMT_S16,
                        48000,
                        &in_ch_layout,
                        codec_ctx->sample_fmt,
                        codec_ctx->sample_rate,
                        0, nullptr);

    swr_init(swr);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // The queue carries ready-to-send packets of exactly PACKET_SIZE bytes.
    // DPP drops the remainder of larger sends and silence-pads smaller ones,
    // so the invariant is enforced here, at the single point of production.
    const size_t PACKET_SIZE = dpp::send_audio_raw_max_length;
    const size_t BYTES_PER_SAMPLE_PAIR = 4; // s16 stereo
    std::vector<uint8_t> staging;

    auto convertIntoStaging = [&](const uint8_t **inData, int inSamples) {
        int maxOutSamples = swr_get_out_samples(swr, inSamples);
        if (maxOutSamples <= 0) return;
        size_t writePos = staging.size();
        staging.resize(writePos + (size_t)maxOutSamples * BYTES_PER_SAMPLE_PAIR);
        uint8_t *outPlane = staging.data() + writePos;
        int converted = swr_convert(swr, &outPlane, maxOutSamples, inData, inSamples);
        staging.resize(writePos + (converted > 0 ? (size_t)converted * BYTES_PER_SAMPLE_PAIR : 0));
    };

    auto pushFullPackets = [&]() {
        size_t readPos = 0;
        while (staging.size() - readPos >= PACKET_SIZE) {
            std::vector<uint8_t> pkt(staging.begin() + readPos,
                                     staging.begin() + readPos + PACKET_SIZE);
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                audioQueue.push(std::move(pkt));
            }
            queueCv.notify_one();
            readPos += PACKET_SIZE;
        }
        staging.erase(staging.begin(), staging.begin() + readPos);
    };

    auto readTime = std::chrono::steady_clock::duration::zero();
    auto decodeTime = std::chrono::steady_clock::duration::zero();

    while (isPlaying) {
        auto t0 = std::chrono::steady_clock::now();
        int ret = av_read_frame(format, packet);
        auto t1 = std::chrono::steady_clock::now();
        readTime += (t1 - t0);

        if (ret < 0) break;

        if (packet->stream_index != audioStream) {
            av_packet_unref(packet);
            continue;
        }

        auto t2 = std::chrono::steady_clock::now();
        if (avcodec_send_packet(codec_ctx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }

        while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
            convertIntoStaging((const uint8_t**)frame->data, frame->nb_samples);
            pushFullPackets();
            av_frame_unref(frame);
        }
        auto t3 = std::chrono::steady_clock::now();
        decodeTime += (t3 - t2);

        av_packet_unref(packet);
    }

    qDebug() << "Total read (network) time: " << std::chrono::duration_cast<std::chrono::milliseconds>(readTime).count() << "ms";
    qDebug() << "Total decode/resample time: " << std::chrono::duration_cast<std::chrono::milliseconds>(decodeTime).count() << "ms";

    // Drain the resampler; only this last packet may be shorter than
    // PACKET_SIZE - DPP silence-pads it, inaudible at end of stream.
    convertIntoStaging(nullptr, 0);
    pushFullPackets();
    if (!staging.empty()) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            audioQueue.push(std::move(staging));
        }
        queueCv.notify_one();
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&swr);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format);

    signalFinished();
}
