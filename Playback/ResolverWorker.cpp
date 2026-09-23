#include "ResolverWorker.h"

#include <memory>
#include <mutex>
#include <string>

#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Song.h"
#include "Labels.h"
#include "Log.h"

#include <dpp/dpp.h>
#include <QDebug>

constexpr size_t LOOKAHEAD = 5;

// Resolves the next few queued songs ahead of time: titles show up in /queue
// and the "Queued at position N" replies, and SongPlayer can start a prefetched
// song without the multi-second yt-dlp pause between tracks. Only a short
// lookahead - direct urls expire within hours, so resolving a 100-song
// playlist up front would be a hundred wasted yt-dlp runs.
ResolverWorker::ResolverWorker(std::deque<Song> &songQueue_,
                               std::condition_variable &stateCv_,
                               std::mutex &stateMutex_)
    : songQueue(songQueue_)
    , stateCv(stateCv_)
    , stateMutex(stateMutex_)
{
    thread = std::thread(&ResolverWorker::run, this);
}

ResolverWorker::~ResolverWorker()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        running = false;
    }
    stateCv.notify_all(); // wake run() so it sees running == false and returns
    if (thread.joinable()) {
        thread.join();
    }
}

void ResolverWorker::run()
{
    logging::nameThisThread("resolver");

    while (running) {
        uint64_t songId = 0;
        std::string target;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            stateCv.wait(lock, [this] {
                return !running || nextUnresolved() != nullptr;
            });
            if (!running) {
                break;
            }
            if (Song *song = nextUnresolved()) {
                songId = song->id;
                target = song->target;
                song->resolveInFlight = true;
            }
        }
        if (songId == 0) {
            continue;
        }

        resolveSongs(songId, target);
    }
}

// call with stateMutex held
Song *ResolverWorker::nextUnresolved()
{
    for (size_t i = 0; i < songQueue.size() && i < LOOKAHEAD; ++i) {
        if (songQueue[i].directUrl.empty() && !songQueue[i].resolveFailed) {
            return &songQueue[i];
        }
    }
    return nullptr;
}

void ResolverWorker::resolveSongs(const uint64_t songId, const std::string &target)
{
    std::unique_ptr<dpp::slashcommand_t> requestEvent;
    std::string label;
    size_t position = 0;

    // Shutdown kills a running yt-dlp instead of waiting it out.
    ResolvedMedia media = WindowsProcessRunner::resolveMedia(target, [this] { return !running.load(); });
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        for (size_t i = 0; i < songQueue.size(); ++i) {
            Song &song = songQueue[i];
            if (song.id != songId) {
                continue;
            }
            song.resolveInFlight = false;
            if (media.directUrl.empty()) {
                // Give up quietly; SongPlayer retries and reports the
                // error to the user when the song's turn comes.
                song.resolveFailed = true;
            } else {
                song.title = media.title;
                song.webpageUrl = media.webpageUrl;
                song.directUrl = media.directUrl;
                song.resolvedAtSeconds = static_cast<int64_t>(time(nullptr));
                // Playlist entries share one reply, the summary - leave it
                // be, and a held song's retries stay quiet.
                if (!song.fromPlaylist && song.failedAttempts == 0) {
                    label = labels::render(song.title, song.webpageUrl, song.target);
                    position = i + 1;
                    requestEvent = std::make_unique<dpp::slashcommand_t>(*song.event);
                }
            }
            break;
        }
    }

    // The worker may be holding off on this very song.
    stateCv.notify_all();

    // Upgrade the "Queued at position N" reply with what we found.
    if (requestEvent) {
        dpp::message queuedInfo(messages::queuedAtPrefix + std::to_string(position) + messages::queuedSeparator + label);
        queuedInfo.set_allowed_mentions();
        requestEvent->edit_original_response(queuedInfo);
    }
}
