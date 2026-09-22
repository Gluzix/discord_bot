#include "PcmBuffer.h"

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

bool PcmBuffer::cutShort() const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return !(decodingFinished && audioQueue.empty());
}

std::mutex &PcmBuffer::mutex()
{
    return queueMutex;
}

std::condition_variable &PcmBuffer::cv()
{
    return queueCv;
}
