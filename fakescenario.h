#ifndef FAKESCENARIO_H
#define FAKESCENARIO_H

#include "common.h"

/*
 * Generated sensor readings for the desktop build (no BLE), deterministic:
 * every sensor drifts slowly around its own base temperature, and a
 * scenario adds the events a real installation meets, to exercise the
 * filters of RoomSense and the sensor loss handling of AcquaThermoNet.
 *
 * Scenario: items separated by ';', S = sensor index (the [SENSORS] groups
 * of the ini in alphabetical order, as QSettings lists them: 0 = first),
 * T = seconds since start, V in 0.1 degC:
 *   lost=S@T        sensor S silent from T on (until a back event)
 *   back=S@T        sensor S heard again from T on
 *   spike=S@T:V     one reading of S near T moved by V (e.g. +80)
 *   out=S@T:V       one reading of S near T is V (e.g. 850 = 85.0 degC)
 *   step=S@T:V      S moved by V from T on (a real change: to be confirmed)
 *   cold=S          S around -5.0 degC (negative temperatures)
 *   nobatt=S        S without battery information (0)
 *   press=S@T:N     at T, N presses of + (or - if negative) on the panel
 *                   for the zone of S (desktop tests of the setpoints)
 *   mode=M@T        at T, house mode M (normal, window, away, boost) chosen on the
 *                   panel (desktop tests of the house mode)
 * Example: RS_FAKE="spike=0@4:+80;out=0@8:850;lost=1@20;cold=2"
 */
class FakeScenario
{
public:
    /* One reading every PERIOD_S seconds per sensor */
    static const int PERIOD_S = 2;

    FakeScenario(int sensors, const QString &spec = QString());

    /* false and error() set if the scenario cannot be parsed: then only
     * the normal drift is generated */
    bool isValid() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }

    /* Reading of sensor at t seconds since start (temp, hum, batt,
     * battmv, pc, rssi); false: the sensor is silent */
    bool reading(int sensor, qint64 t, sensor_env_t &env) const;

    /* Panel presses due in [from, to): sensor and steps */
    QVector<QPair<int, int>> presses(qint64 from, qint64 to) const;
    /* House modes chosen in [from, to): "normal", "window", "away" */
    QStringList modes(qint64 from, qint64 to) const;

private:
    typedef struct {
        enum { Lost, Back, Spike, Out, Step, Press } type;
        int    sensor;
        qint64 at;
        int    value;
    } event_t;

    bool parse(const QString &spec);
    bool silent(int sensor, qint64 t) const;

    int                 m_sensors;
    QVector<event_t>    m_events;
    QVector<QPair<qint64, QString>> m_modes;
    QVector<bool>       m_cold;
    QVector<bool>       m_noBatt;
    QString             m_error;
};

#endif // FAKESCENARIO_H
