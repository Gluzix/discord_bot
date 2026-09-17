#include "SongPlayer.h"
#include "PcmResampler.h"
#include "VoiceDrainWatchdog.h"
#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Labels.h"
#include "Log.h"

#include <dpp/dpp.h>

#include <algorithm>
#include <chrono>
#include <ctime>

SongPlayer::SongPlayer(std::function<void(std::string)> onLabelResolved_)
    : onLabelResolved(std::move(onLabelResolved_))
{
}

void SongPlayer::arm()
{
    isPlaying = true;
}

SongPlayer::Outcome SongPlayer::play(dpp::discord_voice_client *voiceClient, Song &song)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = false;
        audioQueue = {};
        queuedBytes = 0;
        playedBytes = 0;
        seekTicket = 0;
        seekInFlight = false;
        bytesBeforeSeek = 0;
        droppedForSeek = 0;
        songEnding = false;
        flushClient = false;
        activeResampler = nullptr;
        durationSeconds = 0;
    }
    currentSongFailed = false;
    voiceLost = false;

    decoderThread = std::thread(&SongPlayer::decode, this, std::ref(song));
    senderThread = std::thread(&SongPlayer::streamAudio, this, voiceClient);

    // Sender first: once it is joined, this thread is the only one on the
    // voice client, so this is the one place that may call it after a song.
    // It has to happen before the decoder join - a decoder stuck in yt-dlp
    // can outlive stop()'s bounded wait, and past that the client may be gone.
    senderThread.join();

    // dpp may already have destroyed the client it gave up on - not one
    // call more, not even is_paused().
    if (voiceLost) {
        decoderThread.join();
        return Outcome::VoiceLost;
    }

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
    return currentSongFailed ? Outcome::Failed : Outcome::Finished;
}

void SongPlayer::stop()
{
    // isPlaying participates in queueCv wait predicates - flipping it while
    // holding the mutex guarantees no waiter can miss the wakeup.
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        isPlaying = false;
    }
    queueCv.notify_all();
}

std::optional<SongPlayer::Position> SongPlayer::seekBy(int deltaSeconds)
{
    return seek(deltaSeconds, true);
}

std::optional<SongPlayer::Position> SongPlayer::seekTo(int seconds)
{
    return seek(seconds, false);
}

std::optional<SongPlayer::Position> SongPlayer::seek(double seconds, bool relative)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    if (!isPlaying || songEnding || activeResampler == nullptr) {
        return std::nullopt;
    }

    const double current = static_cast<double>(playedBytes) / BYTES_PER_SECOND;
    double target = std::max(relative ? current + seconds : seconds, 0.0);
    if (durationSeconds > 0) {
        target = std::min(target, durationSeconds); // a target at the end just ends the song
    }

    // A forward the queue already holds needs no decoder: drop whole packets.
    const double aheadSeconds = target - current;
    if (!seekInFlight && aheadSeconds > 0
        && aheadSeconds * BYTES_PER_SECOND <= static_cast<double>(queuedBytes)) {
        const size_t aheadBytes = static_cast<size_t>(aheadSeconds * BYTES_PER_SECOND);
        size_t dropped = 0;
        while (!audioQueue.empty() && dropped < aheadBytes) {
            dropped += audioQueue.front().size();
            queuedBytes -= audioQueue.front().size();
            audioQueue.pop();
        }
        playedBytes += dropped;
        flushClient = true;
        const Position position{static_cast<double>(playedBytes) / BYTES_PER_SECOND, durationSeconds};
        lock.unlock();
        queueCv.notify_all();
        return position;
    }

    bytesBeforeSeek = playedBytes;
    droppedForSeek = queuedBytes;
    audioQueue = {};
    queuedBytes = 0;
    seekInFlight = true;   // every packet still in the decoder is from before the jump
    decodingFinished = false;
    flushClient = true;
    ++seekTicket;
    activeResampler->requestSeek(target, seekTicket);
    // Optimistic, so a second jump issued before this one lands builds on it.
    playedBytes = static_cast<uint64_t>(target * BYTES_PER_SECOND);
    lock.unlock();
    queueCv.notify_all();
    return Position{target, durationSeconds};
}

void SongPlayer::streamAudio(dpp::discord_voice_client *voiceClient)
{
    logging::nameThisThread("sender");

    // Everything handed to DPP is opus-encoded and (with DAVE E2EE, the
    // default) encrypted with the *current* group key immediately. The key
    // rotates whenever someone joins or leaves, turning any large queued
    // backlog into silence for the listeners - so keep DPP's queue short
    // and hold the deep buffer here as PCM, which no rekey can spoil.
    const float MAX_BUFFERED_SECONDS = 1.0f;

    // A dropped voice session leaves dpp retrying forever with a send buffer
    // that never drains again. Ten seconds is far outside anything healthy
    // and leaves dpp's own retry chain time to finish or die first.
    const std::chrono::seconds VOICE_DEAD_AFTER(10);
    VoiceDrainWatchdog watchdog(VOICE_DEAD_AFTER);

    // A seek asks for dpp's ~1s tail to go, so the jump is audible at once.
    // This thread owns the client, so it is the one that may flush it.
    auto takeFlushRequest = [this] {
        std::lock_guard<std::mutex> lock(queueMutex);
        const bool wanted = flushClient;
        flushClient = false;
        return wanted;
    };
    auto flushClientNow = [&] {
        // dpp sets terminating at least 100ms before it destroys the client.
        if (voiceClient->terminating) {
            voiceLost = true;
            stop();
            return false;
        }
        voiceClient->stop_audio();
        return true;
    };

    // The decoder fills the queue with ready-to-send packets of exactly
    // dpp::send_audio_raw_max_length bytes; only the final one may be shorter.
    while (isPlaying) {
        std::vector<uint8_t> packet;
        bool flushNow = false;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] {
                return !audioQueue.empty() || decodingFinished || flushClient || !isPlaying;
            });
            if (flushClient) {
                flushClient = false;
                flushNow = true;
            } else if (audioQueue.empty()) {
                if (decodingFinished) {
                    songEnding = true; // nothing left to seek in
                    break;
                }
                continue;
            } else {
                packet = std::move(audioQueue.front());
                audioQueue.pop();
                queuedBytes -= packet.size();
                playedBytes += packet.size();
            }
        }
        if (flushNow) {
            if (!flushClientNow()) break;
            continue;
        }
        queueCv.notify_one(); // room for the decoder

        // Never call into dpp while holding queueMutex - a foreign lock
        // inside our critical section is how the whole pipeline wedges.
        while (isPlaying) {
            if (voiceClient->terminating) {
                voiceLost = true;
                break;
            }
            const float bufferedSeconds = voiceClient->get_secs_remaining();
            if (watchdog.observe(bufferedSeconds, voiceClient->is_paused())) {
                voiceLost = true;
                break;
            }
            if (takeFlushRequest()) {
                flushNow = true;
                break;
            }
            if (bufferedSeconds <= MAX_BUFFERED_SECONDS) {
                break;
            }

            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                return !isPlaying || flushClient;
            });
        }
        if (voiceLost) {
            stop(); // a decoder waiting on a full queue has nobody else to wake it
        }
        if (!isPlaying) break;
        if (flushNow) {
            // The packet in hand is from before the jump - drop it.
            if (!flushClientNow()) break;
            continue;
        }

        if (voiceClient->terminating) {
            voiceLost = true;
            stop();
            break;
        }
        voiceClient->send_audio_raw((uint16_t*)packet.data(), packet.size());
        watchdog.reset();
    }

    // Not a plain store: the decoder waits on queueCv until the song ends.
    stop();
}

void SongPlayer::decode(Song &song)
{
    logging::nameThisThread("decoder");

    // Whatever happens here, the sender waits on the queue and must be
    // released - every exit path has to mark decoding as finished.
    struct FinishGuard { std::function<void()> done; ~FinishGuard() { if (done) done(); } };
    FinishGuard finish{[this] {
        { std::lock_guard<std::mutex> lock(queueMutex); decodingFinished = true; }
        queueCv.notify_one();
    }};

    // Stopped between arm() and here - don't even launch yt-dlp.
    if (!isPlaying) {
        return;
    }

    // A queued song announces itself in a fresh channel message, leaving its
    // "Queued at position N" reply intact as history (also immune to the
    // 15-minute interaction token limit). An immediate song still morphs its
    // "Looking for your song..." placeholder.
    const bool announceInNewMessage = song.wasQueued;
    dpp::slashcommand_t &event = *song.event;
    auto notifyUser = [&event, announceInNewMessage](dpp::message msg) {
        if (announceInNewMessage) {
            msg.channel_id = event.command.channel_id;
            event.owner->message_create(msg);
        } else {
            event.edit_original_response(msg);
        }
    };
    auto fail = [&](const char *msg) {
        currentSongFailed = true;
        notifyUser(dpp::message(msg));
    };

    // googlevideo urls are ip-bound and expire after a few hours; use the
    // resolver's prefetch only while it's still fresh.
    const int64_t FRESH_FOR_SECONDS = 3600;
    bool prefetchIsFresh = !song.directUrl.empty()
        && (static_cast<int64_t>(time(nullptr)) - song.resolvedAtSeconds) < FRESH_FOR_SECONDS;

    ResolvedMedia media;
    if (prefetchIsFresh) {
        media.title = song.title;
        media.webpageUrl = song.webpageUrl;
        media.directUrl = song.directUrl;
    } else {
        // A skip/stop while yt-dlp runs kills it - nobody waits on a resolve
        // nobody wants anymore.
        media = WindowsProcessRunner::resolveMedia(song.target, [this] { return !isPlaying.load(); });
        if (!media.directUrl.empty()) {
            // Re-resolved (expired prefetch): write it back so a loop replay
            // starts from the fresh url with no extra bookkeeping.
            song.title = media.title;
            song.webpageUrl = media.webpageUrl;
            song.directUrl = media.directUrl;
            song.resolvedAtSeconds = static_cast<int64_t>(time(nullptr));
        }
    }

    // A cancelled resolve comes back empty too - check the skip first so it
    // isn't reported as an error.
    if (!isPlaying) {
        return;
    }
    if (media.directUrl.empty()) {
        fail(messages::errorResolve);
        return;
    }

    if (onLabelResolved) {
        onLabelResolved(labels::render(media.title, media.webpageUrl, song.target));
    }

    PcmResampler resampler(dpp::send_audio_raw_max_length);
    switch (resampler.open(media.directUrl)) {
        case PcmResampler::Result::OpenFailed: fail(messages::errorOpenStream); return;
        case PcmResampler::Result::ReadFailed: fail(messages::errorReadStream); return;
        case PcmResampler::Result::NoAudio: fail(messages::errorNoAudio); return;
        case PcmResampler::Result::Ok: break;
    }

    if (!isPlaying) {
        return;
    }

    // The title is untrusted input from the video page - disable every kind
    // of mention so a title like "@everyone" can't ping the server. Rendered
    // as a masked link: clickable title, no embed preview. Song-mode loop
    // replays stay quiet - nobody needs the same title announced 20 times.
    if (!song.isLoopReplay) {
        dpp::message nowPlaying(messages::playingPrefix + labels::render(media.title, media.webpageUrl, song.target));
        nowPlaying.set_allowed_mentions();
        notifyUser(nowPlaying);
    }

    auto sink = [this](std::vector<uint8_t> pkt) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCv.wait(lock, [this] {
            return queuedBytes < MAX_QUEUED_BYTES || seekInFlight || !isPlaying;
        });
        if (!isPlaying || seekInFlight) {
            return; // a packet from before the jump
        }
        queuedBytes += pkt.size();
        audioQueue.push(std::move(pkt));
        lock.unlock();
        queueCv.notify_one();
    };

    auto onSeeked = [this](uint64_t ticket, bool ok) {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (ticket != seekTicket) {
            return; // an older seek: a newer one is still on its way
        }
        seekInFlight = false;
        if (!ok) {
            playedBytes = bytesBeforeSeek + droppedForSeek; // the dropped queue was a forward
        }
        lock.unlock();
        queueCv.notify_all();
    };

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        activeResampler = &resampler;
        durationSeconds = resampler.durationSeconds();
    }

    // The decoder has to outlive end of stream: it runs a minute ahead, so
    // otherwise the last minute of every song couldn't be rewound.
    for (;;) {
        resampler.run(sink, [this] { return isPlaying.load(); }, onSeeked);

        std::unique_lock<std::mutex> lock(queueMutex);
        decodingFinished = true; // the sender may end the song once the queue drains
        queueCv.notify_all();
        queueCv.wait(lock, [this] { return seekInFlight || !isPlaying; });
        if (!isPlaying) {
            break;
        }
        decodingFinished = false; // a rewind after end of stream: decode again
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        activeResampler = nullptr; // it must not outlive the resampler below
    }
}
