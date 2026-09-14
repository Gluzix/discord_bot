#include "SongPlayer.h"
#include "PcmResampler.h"
#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Labels.h"

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

bool SongPlayer::play(dpp::discord_voice_client *voiceClient, Song &song)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = false;
        audioQueue = {};
        queuedBytes = 0;
        bytesToDiscard = 0;
    }
    currentSongFailed = false;

    decoderThread = std::thread(&SongPlayer::decode, this, std::ref(song));
    senderThread = std::thread(&SongPlayer::streamAudio, this, voiceClient);

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
    return !currentSongFailed;
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

int SongPlayer::forward(int seconds)
{
    if (seconds <= 0) {
        return 0;
    }

    // The buffered PCM is dropped right here; whatever it doesn't cover the
    // decoder skips by decoding and discarding. dpp's ~1s send buffer stays
    // untouched on purpose: flushing it means calling the voice client while
    // the sender thread is live on it, and dpp's send path has no lock
    // against that.
    const size_t bytesToSkip = static_cast<size_t>(seconds) * BYTES_PER_SECOND;
    size_t skippedBytes = 0;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!audioQueue.empty() && skippedBytes < bytesToSkip) {
            skippedBytes += audioQueue.front().size();
            queuedBytes -= audioQueue.front().size();
            audioQueue.pop();
        }
        if (skippedBytes < bytesToSkip && !decodingFinished) {
            bytesToDiscard += bytesToSkip - skippedBytes;
            skippedBytes = bytesToSkip;
        }
    }
    queueCv.notify_all(); // a decoder waiting on a full queue has work again

    if (skippedBytes == 0) {
        return 0; // nothing left beyond dpp's own buffer
    }

    // Nearest second, but a real jump never reports as 0 - the reply would
    // claim nothing happened.
    int skipped = static_cast<int>((skippedBytes + BYTES_PER_SECOND / 2) / BYTES_PER_SECOND);
    return std::max(skipped, 1);
}

void SongPlayer::streamAudio(dpp::discord_voice_client *voiceClient)
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
            queuedBytes -= packet.size();
        }
        queueCv.notify_one(); // room for the decoder

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

void SongPlayer::decode(Song &song)
{
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

    resampler.run(
        [this](std::vector<uint8_t> pkt) {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this] {
                return queuedBytes < MAX_QUEUED_BYTES || bytesToDiscard > 0 || !isPlaying;
            });
            if (!isPlaying) {
                return;
            }
            if (bytesToDiscard > 0) {
                // Whole packets only - a trimmed one would be silence-padded by dpp.
                bytesToDiscard -= std::min(bytesToDiscard, pkt.size());
                return;
            }
            queuedBytes += pkt.size();
            audioQueue.push(std::move(pkt));
            lock.unlock();
            queueCv.notify_one();
        },
        [this] { return isPlaying.load(); });
}
