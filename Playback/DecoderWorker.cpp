#include "DecoderWorker.h"
#include "Labels.h"
#include "Log.h"
#include "Messages.h"
#include "PlaybackButtons.h"
#include "WindowsProcessRunner.h"

#include <QDebug>

#include <ctime>

#include <dpp/dpp.h>

// googlevideo urls are ip-bound and expire after a few hours.
constexpr int64_t FRESH_FOR_SECONDS = 3600;

DecoderWorker::DecoderWorker(PcmBuffer &buffer_, Song &song_, std::function<void(std::string)> onLabelResolved_)
    : resampler(dpp::send_audio_raw_max_length)
    , buffer(buffer_)
    , song(song_)
    , onLabelResolved(std::move(onLabelResolved_))
{
    thread = std::thread(&DecoderWorker::run, this);
}

DecoderWorker::~DecoderWorker()
{
    join();
}

void DecoderWorker::join()
{
    if (thread.joinable()) {
        thread.join();
    }
}

bool DecoderWorker::failed() const
{
    return songFailed;
}

void DecoderWorker::run()
{
    logging::nameThisThread("decoder");

    struct FinishGuard { std::function<void()> done; ~FinishGuard() { if (done) done(); } };
    FinishGuard finish{[this] { buffer.markFinished(); }};

    if (!buffer.running()) {
        return;
    }

    ResolvedMedia media = mediaToPlay();

    if (!buffer.running()) {
        return;
    }
    if (media.directUrl.empty()) {
        fail(messages::errorResolve);
        return;
    }

    if (onLabelResolved) {
        onLabelResolved(labels::render(media.title, media.webpageUrl, song.target));
    }

    switch (resampler.open(media.directUrl)) {
        case PcmResampler::Result::OpenFailed: fail(messages::errorOpenStream); return;
        case PcmResampler::Result::ReadFailed: fail(messages::errorReadStream); return;
        case PcmResampler::Result::NoAudio: fail(messages::errorNoAudio); return;
        case PcmResampler::Result::Ok: break;
    }

    if (!buffer.running()) {
        return;
    }

    qDebug().noquote() << "Playing" << QString::fromStdString(media.title) << "-" << QString::fromStdString(media.webpageUrl);

    if (!song.isLoopReplay) {
        dpp::message nowPlaying(messages::playingPrefix + labels::render(media.title, media.webpageUrl, song.target));
        // The title is untrusted input from the video page - disable every
        // kind of mention so a title like "@everyone" can't ping the server.
        nowPlaying.set_allowed_mentions();
        nowPlaying.add_component(buttons::controlRow());
        notifyUser(nowPlaying);
    }

    decode();
}

// A queued song announces itself in a fresh channel message, leaving its
// "Queued at position N" reply intact as history (also immune to the
// 15-minute interaction token limit).
void DecoderWorker::notifyUser(dpp::message msg)
{
    const bool announceInNewMessage = song.wasQueued;
    dpp::slashcommand_t &event = *song.event;

    if (announceInNewMessage) {
        msg.channel_id = event.command.channel_id;
        event.owner->message_create(msg);
    } else {
        event.edit_original_response(msg);
    }
}

void DecoderWorker::fail(const char *msg)
{
    songFailed = true;
    // A held song's retries are quiet - the notice went out once.
    if (song.failedAttempts == 0) {
        notifyUser(dpp::message(msg));
    }
}

ResolvedMedia DecoderWorker::mediaToPlay()
{
    ResolvedMedia media;

    bool prefetchIsFresh = !song.directUrl.empty()
                           && (static_cast<int64_t>(time(nullptr)) - song.resolvedAtSeconds) < FRESH_FOR_SECONDS;

    if (prefetchIsFresh) {
        media.title = song.title;
        media.webpageUrl = song.webpageUrl;
        media.directUrl = song.directUrl;
    } else {
        media = WindowsProcessRunner::resolveMedia(song.target, [this] { return !buffer.running(); });
        if (!media.directUrl.empty()) {
            // Written back so a loop replay starts from the fresh url with no
            // extra bookkeeping.
            song.title = media.title;
            song.webpageUrl = media.webpageUrl;
            song.directUrl = media.directUrl;
            song.resolvedAtSeconds = static_cast<int64_t>(time(nullptr));
        }
    }
    return media;
}

void DecoderWorker::decode()
{
    auto sink = [this](std::vector<uint8_t> pkt) {
        buffer.push(std::move(pkt));
    };

    auto onSeeked = [this](uint64_t ticket, bool ok) {
        buffer.seekApplied(ticket, ok);
    };

    buffer.setDuration(resampler.durationSeconds());
    buffer.setSeekRequester([this](double seconds, uint64_t ticket) {
        resampler.requestSeek(seconds, ticket);
    });

    // A rewind re-runs a stream that is already gone - say it once per song.
    bool streamLostAnnounced = false;

    dpp::slashcommand_t &event = *song.event;

    for (;;) {
        const PcmResampler::RunEnd runEnd = resampler.run(sink, [this] { return buffer.running(); }, onSeeked);
        if (runEnd == PcmResampler::RunEnd::ReadError && !streamLostAnnounced) {
            streamLostAnnounced = true;
            qDebug() << "The audio stream died mid-song - telling the channel";
            // Never an edit: the "Playing:" reply stays as history.
            dpp::message lost(messages::streamLost);
            lost.channel_id = event.command.channel_id;
            lost.set_allowed_mentions();
            event.owner->message_create(lost);
        }

        if (!buffer.finishedAndWaitForRewind()) {
            break;
        }
    }

    buffer.clearSeekRequester();
}
