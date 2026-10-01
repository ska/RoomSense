#ifndef BLESTATUS_H
#define BLESTATUS_H

#include <atomic>
#include "monoclock.h"

/*
 * State of the BLE reader thread for the diagnostic page: written by the
 * thread, read by the GUI.
 */
class BleStatus
{
public:
    enum State { Starting, Scanning, Retrying, Simulated };

    void set(State state, int device = -1) { m_device.store(device); m_state.store(state); }
    /* An advertisement arrived (any device, not only the sensors) */
    void advertisement() { m_lastAdvMs.store(MonoClock::nowMs()); }

    State state() const { return State(m_state.load()); }
    /* hci<N> while scanning, -1 otherwise */
    int device() const { return m_device.load(); }
    /* MonoClock of the last advertisement, 0 = none yet */
    qint64 lastAdvertisementMs() const { return m_lastAdvMs.load(); }

private:
    std::atomic<int>    m_state { Starting };
    std::atomic<int>    m_device { -1 };
    std::atomic<qint64> m_lastAdvMs { 0 };
};

#endif // BLESTATUS_H
