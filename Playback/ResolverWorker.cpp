#include "ResolverWorker.h"

#include <memory>
#include <mutex>
#include <string>

#include "WindowsProcessRunner.h"
#include "Messages.h"
#include "Song.h"
#include "Labels.h"

#include <dpp/dpp.h>
#include <QDebug>

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
    static constexpr size_t LOOKAHEAD = 5;
    auto nextUnresolved = [this]() -> const Song* { // call with stateMutex held
        for (size_t i = 0; i < songQueue.size() && i < LOOKAHEAD; ++i) {
            if (songQueue[i].directUrl.empty() && !songQueue[i].resolveFailed) {
                return &songQueue[i];
            }
        }
        return nullptr;
    };

    while (running) {
        uint64_t songId = 0;
        std::string target;
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            stateCv.wait(lock, [this, &nextUnresolved] {
                return !running || nextUnresolved() != nullptr;
            });
            if (!running) {
                break;
            }
            if (const Song *song = nextUnresolved()) {
                songId = song->id;
                target = song->target;
            }
        }
        if (songId == 0) {
            continue;
        }

        // Shutdown kills a running yt-dlp instead of waiting it out.
        ResolvedMedia media = WindowsProcessRunner::resolveMedia(target, [this] { return !running.load(); });

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
                    // Give up quietly; SongPlayer retries and reports the
                    // error to the user when the song's turn comes.
                    song.resolveFailed = true;
                } else {
                    song.title = media.title;
                    song.webpageUrl = media.webpageUrl;
                    song.directUrl = media.directUrl;
                    song.resolvedAtSeconds = static_cast<int64_t>(time(nullptr));
                    // Playlist entries share one reply, the summary - leave it be.
                    if (!song.fromPlaylist) {
                        label = labels::render(song.title, song.webpageUrl, song.target);
                        position = i + 1;
                        requestEvent = std::make_unique<dpp::slashcommand_t>(*song.event);
                    }
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
