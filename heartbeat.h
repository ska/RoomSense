#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <atomic>
#include "monoclock.h"

/*
 * Liveness of a worker thread, for the watchdog: the thread calls beat()
 * at least every few seconds while it works (also while it waits to
 * retry), the main thread checks isAlive().
 */
class Heartbeat
{
public:
    Heartbeat() : m_lastMs(MonoClock::nowMs()) {}

    void beat() { m_lastMs.store(MonoClock::nowMs()); }

    /* false if no beat for more than maxAgeMs */
    bool isAlive(qint64 maxAgeMs) const { return MonoClock::nowMs() - m_lastMs.load() <= maxAgeMs; }

private:
    std::atomic<qint64> m_lastMs;
};

#endif // HEARTBEAT_H
