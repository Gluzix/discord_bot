#include "PcmBuffer.h"
#include "Check.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using Kind = PcmBuffer::Next::Kind;
using PushResult = PcmBuffer::PushResult;
using std::chrono::milliseconds;

static const size_t BYTES_PER_SECOND = 192000;
static const size_t PACKET_BYTES = 19200; // 0.1 s, and it divides the cap
static const size_t CAP_PACKETS = 60 * BYTES_PER_SECOND / PACKET_BYTES;

static bool nearly(double a, double b)
{
    return (a - b) < 1e-6 && (b - a) < 1e-6;
}

static std::vector<uint8_t> packet()
{
    return std::vector<uint8_t>(PACKET_BYTES, 0);
}

static bool waitFor(std::atomic<bool> &flag, milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(milliseconds(2));
    }
    return flag;
}

struct RequesterLog
{
    std::atomic<int> calls{0};
    std::atomic<double> seconds{-1.0};
    std::atomic<uint64_t> ticket{0};
};

static void listen(PcmBuffer &buffer, RequesterLog &log)
{
    buffer.setSeekRequester([&log](double seconds, uint64_t ticket) {
        log.seconds = seconds;
        log.ticket = ticket;
        ++log.calls;
    });
}

int main()
{
    { // the cap
        PcmBuffer buffer;
        buffer.arm();
        for (size_t i = 0; i < CAP_PACKETS; ++i) {
            buffer.push(packet());
        }
        std::atomic<bool> pushed{false};
        std::atomic<int> result{-1};
        std::thread producer([&] {
            result = static_cast<int>(buffer.push(packet()));
            pushed = true;
        });
        check(!waitFor(pushed, milliseconds(200)), "push blocks at the 60 s cap");
        const PcmBuffer::Next taken = buffer.next();
        check(taken.kind == Kind::Packet && taken.packet.size() == PACKET_BYTES, "next() pops a whole packet");
        check(waitFor(pushed, milliseconds(1000)), "the pop releases the blocked push");
        producer.join();
        check(result == static_cast<int>(PushResult::Queued), "the released push queues its packet");
        buffer.stop();
    }

    { // stop() wakes a blocked producer
        PcmBuffer buffer;
        buffer.arm();
        for (size_t i = 0; i < CAP_PACKETS; ++i) {
            buffer.push(packet());
        }
        std::atomic<bool> pushed{false};
        std::atomic<int> result{-1};
        std::thread producer([&] {
            result = static_cast<int>(buffer.push(packet()));
            pushed = true;
        });
        check(!waitFor(pushed, milliseconds(200)), "a producer at the cap stays blocked");
        buffer.stop();
        check(waitFor(pushed, milliseconds(1000)), "stop() wakes a producer blocked in push");
        producer.join();
        check(result == static_cast<int>(PushResult::Stopped), "the woken push answers Stopped");
    }

    { // stop() wakes a blocked consumer
        PcmBuffer buffer;
        buffer.arm();
        std::atomic<bool> returned{false};
        std::atomic<int> kind{-1};
        std::thread consumer([&] {
            kind = static_cast<int>(buffer.next().kind);
            returned = true;
        });
        check(!waitFor(returned, milliseconds(200)), "next() blocks on an empty, unfinished queue");
        buffer.stop();
        check(waitFor(returned, milliseconds(1000)), "stop() wakes a consumer blocked in next");
        consumer.join();
        check(kind == static_cast<int>(Kind::Stopped), "the woken next answers Stopped");
    }

    { // stop() wakes a pacing wait
        PcmBuffer buffer;
        buffer.arm();
        std::atomic<bool> returned{false};
        std::thread waiter([&] {
            buffer.pacingWait(milliseconds(2000));
            returned = true;
        });
        check(!waitFor(returned, milliseconds(200)), "pacingWait waits while nothing happens");
        buffer.stop();
        check(waitFor(returned, milliseconds(500)), "stop() wakes a pacingWait");
        waiter.join();
    }

    { // a real seek: stale until the newest ticket lands
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        buffer.push(packet());

        const auto first = buffer.seek(100, false);
        check(first && nearly(first->seconds, 100) && nearly(first->durationSeconds, 300), "a real seek reports its target");
        check(log.calls == 1 && nearly(log.seconds, 100) && log.ticket == 1, "the requester got the target and ticket 1");
        check(buffer.push(packet()) == PushResult::Stale, "a push during the seek is Stale");

        const auto second = buffer.seek(200, false);
        check(second && nearly(second->seconds, 200), "a second seek while one is in flight is real too");
        check(log.calls == 2 && nearly(log.seconds, 200) && log.ticket == 2, "the requester got the target and ticket 2");

        buffer.seekApplied(1, true);
        check(buffer.push(packet()) == PushResult::Stale, "an older ticket changes nothing");
        buffer.seekApplied(2, true);
        check(buffer.push(packet()) == PushResult::Queued, "the newest ticket ends the staleness");
        buffer.stop();
    }

    { // the packet a blocked producer is holding is stale too
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        for (size_t i = 0; i < CAP_PACKETS; ++i) {
            buffer.push(packet());
        }
        std::atomic<bool> pushed{false};
        std::atomic<int> result{-1};
        std::thread producer([&] {
            result = static_cast<int>(buffer.push(packet()));
            pushed = true;
        });
        check(!waitFor(pushed, milliseconds(200)), "a producer at the cap blocks before the seek");
        buffer.seek(100, false);
        check(waitFor(pushed, milliseconds(1000)), "a real seek releases the blocked producer");
        producer.join();
        check(result == static_cast<int>(PushResult::Stale), "the packet it was holding is Stale");
        buffer.stop();
    }

    { // the fast path forward
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        for (int i = 0; i < 10; ++i) { // 1.0 s queued
            buffer.push(packet());
        }

        const auto jumped = buffer.seek(0.5, true);
        check(jumped && nearly(jumped->seconds, 0.5) && nearly(jumped->durationSeconds, 300), "the fast path reports the new position");
        check(log.calls == 0, "the fast path calls no requester");
        check(buffer.takeFlushRequest(), "the fast path asks for a client flush");

        int popped = 0;
        buffer.markFinished();
        for (;;) {
            const PcmBuffer::Next taken = buffer.next();
            if (taken.kind != Kind::Packet) {
                check(taken.kind == Kind::Ended, "the drained queue ends the song");
                break;
            }
            ++popped;
        }
        check(popped == 5, "it dropped whole packets: five of ten left");
        buffer.stop();
    }

    { // a failed seek restores the position
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        for (int i = 0; i < 10; ++i) {
            buffer.push(packet());
        }
        for (int i = 0; i < 3; ++i) { // 0.3 s played, 0.7 s still queued
            buffer.next();
        }
        const auto jumped = buffer.seek(100, false);
        check(jumped && nearly(jumped->seconds, 100), "the optimistic position is the target");
        buffer.seekApplied(log.ticket, false);
        const auto restored = buffer.position();
        check(restored && nearly(restored->seconds, 1.0), "a failed seek restores played plus dropped bytes");
        buffer.stop();
    }

    { // Ended, and a seek after it
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        buffer.push(packet());
        buffer.push(packet());
        buffer.markFinished();
        check(buffer.next().kind == Kind::Packet, "a finished decoder still hands out its first packet");
        check(buffer.next().kind == Kind::Packet, "and its second");
        check(buffer.next().kind == Kind::Ended, "Ended only once finished and empty");
        check(!buffer.seek(10, false), "a seek after Ended is refused");
        buffer.stop();
    }

    { // the rewind wait, revived by a seek
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        std::atomic<bool> returned{false};
        std::atomic<int> again{-1};
        std::thread decoder([&] {
            again = buffer.finishedAndWaitForRewind() ? 1 : 0;
            returned = true;
        });
        check(!waitFor(returned, milliseconds(200)), "finishedAndWaitForRewind blocks at end of stream");
        buffer.seek(50, false);
        check(waitFor(returned, milliseconds(1000)), "a seek releases the rewind wait");
        decoder.join();
        check(again == 1, "...and asks for another decode");
        buffer.stop();
    }

    { // the rewind wait, released by a stop
        PcmBuffer buffer;
        buffer.arm();
        std::atomic<bool> returned{false};
        std::atomic<int> again{-1};
        std::thread decoder([&] {
            again = buffer.finishedAndWaitForRewind() ? 1 : 0;
            returned = true;
        });
        check(!waitFor(returned, milliseconds(200)), "the rewind wait blocks until something happens");
        buffer.stop();
        check(waitFor(returned, milliseconds(1000)), "a stop releases the rewind wait");
        decoder.join();
        check(again == 0, "...and ends the decoder");
    }

    { // a flush request wakes the pacing wait, and is reported once
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        for (int i = 0; i < 10; ++i) {
            buffer.push(packet());
        }
        std::atomic<bool> returned{false};
        std::thread waiter([&] {
            buffer.pacingWait(milliseconds(2000));
            returned = true;
        });
        check(!waitFor(returned, milliseconds(200)), "pacingWait waits with no flush pending");
        buffer.seek(0.5, true); // the fast path sets the flush request
        check(waitFor(returned, milliseconds(500)), "a flush request wakes pacingWait early");
        waiter.join();
        check(buffer.takeFlushRequest(), "takeFlushRequest reports the flush");
        check(!buffer.takeFlushRequest(), "...and only once");

        buffer.seek(100, false); // a real seek asks for a flush too
        check(buffer.takeFlushRequest(), "a real seek asks for a client flush");
        buffer.stop();
    }

    { // next() reports a flush once, then goes back to packets
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        for (int i = 0; i < 10; ++i) {
            buffer.push(packet());
        }
        buffer.seek(0.2, true); // the fast path: two packets dropped, a flush asked
        check(buffer.next().kind == Kind::Flush, "next() reports a pending flush first");
        check(buffer.next().kind == Kind::Packet, "...once, then packets again");
        buffer.stop();
    }

    { // position arithmetic: pops
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(12.5);
        listen(buffer, log);
        for (int i = 0; i < 20; ++i) {
            buffer.push(packet());
        }
        for (int i = 0; i < 4; ++i) {
            buffer.next();
        }
        const auto position = buffer.position();
        check(position && nearly(position->seconds, 0.4), "four pops move the position by four packets");
        buffer.stop();
    }

    { // position arithmetic: a fast-path drop and a real seek
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(12.5);
        listen(buffer, log);
        for (int i = 0; i < 10; ++i) {
            buffer.push(packet());
        }
        const auto dropped = buffer.seek(0.3, true);
        check(dropped && nearly(dropped->seconds, 0.3), "a fast-path drop advances by the dropped bytes");
        check(nearly(dropped->durationSeconds, 12.5), "the position carries the duration that was set");

        const auto jumped = buffer.seek(5, false);
        check(jumped && nearly(jumped->seconds, 5), "a real seek reports its optimistic target");
        buffer.seekApplied(log.ticket, true);
        const auto landed = buffer.position();
        check(landed && nearly(landed->seconds, 5), "...and the position is there");
        buffer.stop();
    }

    { // position arithmetic: clamping
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(12.5);
        listen(buffer, log);
        const auto jumped = buffer.seek(1000, false);
        check(jumped && nearly(jumped->seconds, 12.5), "a target past the end is clamped to the duration");
        check(nearly(log.seconds, 12.5), "the requester was asked for the clamped target");
        buffer.stop();
    }

    { // position(): a read, not a seek
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(12.5);
        check(!buffer.position(), "position() is nullopt before a requester is set");
        listen(buffer, log);
        const auto start = buffer.position();
        check(start && nearly(start->seconds, 0), "position() starts at zero");
        check(start && nearly(start->durationSeconds, 12.5), "position() carries the duration that was set");

        for (int i = 0; i < 10; ++i) {
            buffer.push(packet());
        }
        for (int i = 0; i < 3; ++i) {
            buffer.next();
        }
        const auto popped = buffer.position();
        check(popped && nearly(popped->seconds, 0.3), "three pops move position() by three packets");
        check(log.calls == 0 && !buffer.takeFlushRequest(), "reading position() asks for no seek and no flush");

        buffer.markFinished();
        int left = 0;
        while (buffer.next().kind == Kind::Packet) {
            ++left;
        }
        check(left == 7, "reading position() drops nothing from the queue");
        check(!buffer.position(), "position() is nullopt after Ended");
        buffer.stop();
    }

    { // position(): a real seek, then a stop
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        buffer.seek(100, false);
        const auto jumped = buffer.position();
        check(jumped && nearly(jumped->seconds, 100), "position() shows a real seek's optimistic target");
        buffer.stop();
        check(!buffer.position(), "position() is nullopt once stopped");
    }

    { // reset
        PcmBuffer buffer;
        RequesterLog log;
        buffer.arm();
        buffer.setDuration(300);
        listen(buffer, log);
        for (int i = 0; i < 5; ++i) {
            buffer.push(packet());
        }
        buffer.seek(100, false);
        buffer.markFinished();

        buffer.reset();
        check(buffer.running(), "reset leaves an armed buffer armed");
        check(!buffer.takeFlushRequest(), "reset clears the flush request");
        check(!buffer.seek(10, false), "reset clears the seek requester");

        std::atomic<bool> returned{false};
        std::atomic<int> kind{-1};
        std::thread consumer([&] {
            kind = static_cast<int>(buffer.next().kind);
            returned = true;
        });
        check(!waitFor(returned, milliseconds(200)), "reset clears the queue and the finished flag");
        check(buffer.push(packet()) == PushResult::Queued, "reset clears the seek in flight");
        check(waitFor(returned, milliseconds(1000)) && kind == static_cast<int>(Kind::Packet), "the consumer gets the new packet");
        consumer.join();

        RequesterLog after;
        listen(buffer, after);
        const auto position = buffer.position();
        check(position && nearly(position->seconds, 0.1), "reset restarts the position at zero");
        check(position && nearly(position->durationSeconds, 0), "reset clears the duration");
        buffer.stop();

        PcmBuffer idle;
        idle.reset();
        check(!idle.running(), "reset does not arm a buffer that was not armed");
    }

    return summary();
}
