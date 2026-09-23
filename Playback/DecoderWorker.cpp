#include "DecoderWorker.h"
#include "Messages.h"
#include "Labels.h"
#include "PlaybackButtons.h"
#include "Log.h"

#include <dpp/dpp.h>
#include <QDebug>

#include <ctime>

const int64_t FRESH_FOR_SECONDS = 3600;

DecoderWorker::DecoderWorker(PcmBuffer &buffer_, Song &song_, std::function<void(std::string)> onLabelResolved_)
    : buffer(buffer_)
    , song(song_)
    , onLabelResolved(std::move(onLabelResolved_))
    , resampler(dpp::send_audio_raw_max_length)
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

    // Whatever happens here, the sender waits on the queue and must be
    // released - every exit path has to mark decoding as finished.
    struct FinishGuard { std::function<void()> done; ~FinishGuard() { if (done) done(); } };
    FinishGuard finish{[this] { buffer.markFinished(); }};

    // Stopped between arm() and here - don't even launch yt-dlp.
    if (!buffer.running()) {
        return;
    }

    ResolvedMedia media = computeResolveMedia();

    // A cancelled resolve comes back empty too - check the skip first so it
    // isn't reported as an error.
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

    // The title is untrusted input from the video page - disable every kind
    // of mention so a title like "@everyone" can't ping the server. Rendered
    // as a masked link: clickable title, no embed preview. Song-mode loop
    // replays stay quiet - nobody needs the same title announced 20 times.
    if (!song.isLoopReplay) {
        dpp::message nowPlaying(messages::playingPrefix + labels::render(media.title, media.webpageUrl, song.target));
        nowPlaying.set_allowed_mentions();
        nowPlaying.add_component(buttons::controlRow());
        notifyUser(nowPlaying);
    }

    innerRun();
}

// A queued song announces itself in a fresh channel message, leaving its
// "Queued at position N" reply intact as history (also immune to the
// 15-minute interaction token limit). An immediate song still morphs its
// "Looking for your song..." placeholder.
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

ResolvedMedia DecoderWorker::computeResolveMedia()
{
    ResolvedMedia media;

    // googlevideo urls are ip-bound and expire after a few hours; use the
    // resolver's prefetch only while it's still fresh.
    bool prefetchIsFresh = !song.directUrl.empty()
                           && (static_cast<int64_t>(time(nullptr)) - song.resolvedAtSeconds) < FRESH_FOR_SECONDS;

    if (prefetchIsFresh) {
        media.title = song.title;
        media.webpageUrl = song.webpageUrl;
        media.directUrl = song.directUrl;
    } else {
        // A skip/stop while yt-dlp runs kills it - nobody waits on a resolve
        // nobody wants anymore.
        media = WindowsProcessRunner::resolveMedia(song.target, [this] { return !buffer.running(); });
        if (!media.directUrl.empty()) {
            // Re-resolved (expired prefetch): write it back so a loop replay
            // starts from the fresh url with no extra bookkeeping.
            song.title = media.title;
            song.webpageUrl = media.webpageUrl;
            song.directUrl = media.directUrl;
            song.resolvedAtSeconds = static_cast<int64_t>(time(nullptr));
        }
    }
    return media;
}

void DecoderWorker::innerRun()
{
    auto sink = [this](std::vector<uint8_t> pkt) {
        buffer.push(std::move(pkt));
    };

    auto onSeeked = [this](uint64_t ticket, bool ok) {
        buffer.seekApplied(ticket, ok);
    };

    // The duration first: it clamps a seek, and a seek is possible the moment
    // the requester is published.
    buffer.setDuration(resampler.durationSeconds());
    buffer.setSeekRequester([this](double seconds, uint64_t ticket) {
        resampler.requestSeek(seconds, ticket);
    });

    // A rewind re-runs a stream that is already gone - say it once per song.
    bool streamLostAnnounced = false;

    // The decoder has to outlive end of stream: it runs a minute ahead, so
    // otherwise the last minute of every song couldn't be rewound.
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

    buffer.clearSeekRequester(); // it must not outlive the resampler below
}
