#include "SongPlayer.h"
#include "PcmResampler.h"
#include "VoiceDrainWatchdog.h"
#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Labels.h"
#include "Log.h"

#include <dpp/dpp.h>
#include <QDebug>

#include <chrono>
#include <ctime>

SongPlayer::SongPlayer(std::function<void(std::string)> onLabelResolved_)
    : onLabelResolved(std::move(onLabelResolved_))
{
}

void SongPlayer::arm()
{
    pcmBuffer.arm();
}

SongPlayer::Outcome SongPlayer::play(dpp::discord_voice_client *voiceClient, Song &song)
{
    pcmBuffer.reset();
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
    if (pcmBuffer.cutShort()) {
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
    pcmBuffer.stop();
}

std::optional<SongPlayer::Position> SongPlayer::seekBy(int deltaSeconds)
{
    return pcmBuffer.seek(deltaSeconds, true);
}

std::optional<SongPlayer::Position> SongPlayer::seekTo(int seconds)
{
    return pcmBuffer.seek(seconds, false);
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

    // This thread owns the client, so it is the one that may flush it.
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
    while (pcmBuffer.running()) {
        PcmBuffer::Next taken = pcmBuffer.next();
        if (taken.kind == PcmBuffer::Next::Kind::Stopped || taken.kind == PcmBuffer::Next::Kind::Ended) {
            break;
        }
        if (taken.kind == PcmBuffer::Next::Kind::Flush) {
            if (!flushClientNow()) break;
            continue;
        }

        std::vector<uint8_t> packet = std::move(taken.packet);
        bool flushNow = false;

        // Never call into dpp while holding the buffer's mutex - a foreign lock
        // inside our critical section is how the whole pipeline wedges.
        while (pcmBuffer.running()) {
            if (voiceClient->terminating) {
                voiceLost = true;
                break;
            }
            const float bufferedSeconds = voiceClient->get_secs_remaining();
            if (watchdog.observe(bufferedSeconds, voiceClient->is_paused())) {
                voiceLost = true;
                break;
            }
            if (pcmBuffer.takeFlushRequest()) {
                flushNow = true;
                break;
            }
            if (bufferedSeconds <= MAX_BUFFERED_SECONDS) {
                break;
            }

            pcmBuffer.pacingWait(std::chrono::milliseconds(100));
        }
        if (voiceLost) {
            stop(); // a decoder waiting on a full queue has nobody else to wake it
        }
        if (!pcmBuffer.running()) break;
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

    // Not a plain store: the decoder waits inside the buffer until the song ends.
    stop();
}

void SongPlayer::decode(Song &song)
{
    logging::nameThisThread("decoder");

    // Whatever happens here, the sender waits on the queue and must be
    // released - every exit path has to mark decoding as finished.
    struct FinishGuard { std::function<void()> done; ~FinishGuard() { if (done) done(); } };
    FinishGuard finish{[this] { pcmBuffer.markFinished(); }};

    // Stopped between arm() and here - don't even launch yt-dlp.
    if (!pcmBuffer.running()) {
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
        // A held song's retries are quiet - the notice went out once.
        if (song.failedAttempts == 0) {
            notifyUser(dpp::message(msg));
        }
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
        media = WindowsProcessRunner::resolveMedia(song.target, [this] { return !pcmBuffer.running(); });
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
    if (!pcmBuffer.running()) {
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

    if (!pcmBuffer.running()) {
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
        pcmBuffer.push(std::move(pkt));
    };

    auto onSeeked = [this](uint64_t ticket, bool ok) {
        pcmBuffer.seekApplied(ticket, ok);
    };

    // The duration first: it clamps a seek, and a seek is possible the moment
    // the requester is published.
    pcmBuffer.setDuration(resampler.durationSeconds());
    pcmBuffer.setSeekRequester([&resampler](double seconds, uint64_t ticket) {
        resampler.requestSeek(seconds, ticket);
    });

    // A rewind re-runs a stream that is already gone - say it once per song.
    bool streamLostAnnounced = false;

    // The decoder has to outlive end of stream: it runs a minute ahead, so
    // otherwise the last minute of every song couldn't be rewound.
    for (;;) {
        const PcmResampler::RunEnd runEnd = resampler.run(sink, [this] { return pcmBuffer.running(); }, onSeeked);
        if (runEnd == PcmResampler::RunEnd::ReadError && !streamLostAnnounced) {
            streamLostAnnounced = true;
            qDebug() << "The audio stream died mid-song - telling the channel";
            // Never an edit: the "Playing:" reply stays as history.
            dpp::message lost(messages::streamLost);
            lost.channel_id = event.command.channel_id;
            lost.set_allowed_mentions();
            event.owner->message_create(lost);
        }

        if (!pcmBuffer.finishedAndWaitForRewind()) {
            break;
        }
    }

    pcmBuffer.clearSeekRequester(); // it must not outlive the resampler below
}
