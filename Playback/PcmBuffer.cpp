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

std::mutex &PcmBuffer::mutex()
{
    return queueMutex;
}

std::condition_variable &PcmBuffer::cv()
{
    return queueCv;
}
