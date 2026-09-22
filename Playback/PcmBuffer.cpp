#include "PcmBuffer.h"

#include <algorithm>

void PcmBuffer::arm()
{
    isPlaying = true;
}

void PcmBuffer::stop()
{
    // isPlaying participates in queueCv wait predicates - flipping it while
    // holding the mutex guarantees no waiter can miss the wakeup.
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
        return PushResult::Stale; // a packet from before the jump
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
    decodingFinished = true; // the sender may end the song once the queue drains
    queueCv.notify_all();
    queueCv.wait(lock, [this] { return seekInFlight || !isPlaying; });
    if (!isPlaying) {
        return false;
    }
    decodingFinished = false; // a rewind after end of stream: decode again
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
        return; // an older seek: a newer one is still on its way
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
    if (audioQueue.empty()) { // so decoding is finished: nothing more is coming
        songEnding = true;    // nothing left to seek in
        return Next{Next::Kind::Ended, {}};
    }
    Next taken{Next::Kind::Packet, std::move(audioQueue.front())};
    audioQueue.pop();
    queuedBytes -= taken.packet.size();
    playedBytes += taken.packet.size();
    lock.unlock();
    queueCv.notify_one(); // room for the decoder
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
