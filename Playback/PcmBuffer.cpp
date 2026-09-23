#include "PcmBuffer.h"

#include <algorithm>

void PcmBuffer::arm()
{
    isPlaying = true;
}

void PcmBuffer::stop()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        isPlaying = false;
    }
    queueCv.notify_all();
}

bool PcmBuffer::running() const
{
    return isPlaying;
}

void PcmBuffer::reset()
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
    durationSeconds = 0;
    seekRequester = nullptr;
}

PcmBuffer::PushResult PcmBuffer::push(std::vector<uint8_t> packet)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCv.wait(lock, [this] {
        return queuedBytes < MAX_QUEUED_BYTES || seekInFlight || !isPlaying;
    });
    if (!isPlaying) {
        return PushResult::Stopped;
    }
    if (seekInFlight) {
        return PushResult::Stale;
    }
    queuedBytes += packet.size();
    audioQueue.push(std::move(packet));
    lock.unlock();
    queueCv.notify_one();
    return PushResult::Queued;
}

void PcmBuffer::markFinished()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        decodingFinished = true;
    }
    queueCv.notify_all();
}

bool PcmBuffer::finishedAndWaitForRewind()
{
    std::unique_lock<std::mutex> lock(queueMutex);
    decodingFinished = true;
    queueCv.notify_all();
    queueCv.wait(lock, [this] { return seekInFlight || !isPlaying; });
    if (!isPlaying) {
        return false;
    }
    decodingFinished = false;
    return true;
}

void PcmBuffer::setDuration(double seconds)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    durationSeconds = seconds;
}

void PcmBuffer::setSeekRequester(SeekRequester requester)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    seekRequester = std::move(requester);
}

void PcmBuffer::clearSeekRequester()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    seekRequester = nullptr;
}

void PcmBuffer::seekApplied(uint64_t ticket, bool ok)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    if (ticket != seekTicket) {
        return;
    }
    seekInFlight = false;
    if (!ok) {
        playedBytes = bytesBeforeSeek + droppedForSeek; // the dropped queue was a forward
    }
    lock.unlock();
    queueCv.notify_all();
}

bool PcmBuffer::cutShort() const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return !(decodingFinished && audioQueue.empty());
}

std::optional<PcmBuffer::Position> PcmBuffer::seek(double seconds, bool relative)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    if (!isPlaying || songEnding || !seekRequester) {
        return std::nullopt;
    }

    const double current = static_cast<double>(playedBytes) / BYTES_PER_SECOND;
    double target = std::max(relative ? current + seconds : seconds, 0.0);
    if (durationSeconds > 0) {
        target = std::min(target, durationSeconds); // a target at the end just ends the song
    }

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
    seekInFlight = true;
    decodingFinished = false;
    flushClient = true;
    ++seekTicket;
    seekRequester(target, seekTicket);
    // Optimistic, so a second jump issued before this one lands builds on it.
    playedBytes = static_cast<uint64_t>(target * BYTES_PER_SECOND);
    const Position position{target, durationSeconds};
    lock.unlock();
    queueCv.notify_all();
    return position;
}

PcmBuffer::Next PcmBuffer::next()
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCv.wait(lock, [this] {
        return !audioQueue.empty() || decodingFinished || flushClient || !isPlaying;
    });
    if (!isPlaying) {
        return Next{Next::Kind::Stopped, {}};
    }
    if (flushClient) {
        flushClient = false;
        return Next{Next::Kind::Flush, {}};
    }
    if (audioQueue.empty()) {
        songEnding = true;
        return Next{Next::Kind::Ended, {}};
    }
    Next taken{Next::Kind::Packet, std::move(audioQueue.front())};
    audioQueue.pop();
    queuedBytes -= taken.packet.size();
    playedBytes += taken.packet.size();
    lock.unlock();
    queueCv.notify_one();
    return taken;
}

bool PcmBuffer::takeFlushRequest()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    const bool wanted = flushClient;
    flushClient = false;
    return wanted;
}

void PcmBuffer::pacingWait(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCv.wait_for(lock, timeout, [this] { return !isPlaying || flushClient; });
}
