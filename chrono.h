#ifndef CHRONO_H
#define CHRONO_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

/*
 * Chrono thermostat of a zone as AcquaThermoNet publishes it (HA_Hmi_Relay
 * docs/SENSOR_GATEWAY_INTERFACE.md §9): the program on <zone>/chrono/profile,
 * the state on <zone>/chrono, the program sent on <zone>/chrono/profile/set.
 * Pure functions, no MQTT: the day and slot functions are those of
 * AcquaThermoNet chrono.cpp, for the chrono page and the editor.
 */

/* From this time on, this setpoint */
struct ChronoSlot
{
    QTime   at;
    double  temp = 0;

    bool operator==(const ChronoSlot &o) const { return at == o.at && temp == o.temp; }
};

/* The program of a zone (<zone>/chrono/profile) */
struct ChronoConfig
{
    /* Slots per profile (AcquaThermoNet ChronoConfig::MAX_SLOTS) */
    static constexpr int MAX_SLOTS = 8;

    bool                enabled     = false;
    bool                edited      = false;    /* edited on a panel, else the controller configuration */
    QVector<ChronoSlot> weekday;                /* sorted by time */
    QVector<ChronoSlot> holiday;                /* sorted by time, empty: weekday */
    quint8              holidayDays = 0;        /* bit n = day n (QDate::dayOfWeek, 1 = Monday) */
};

/* The chrono state of a zone (<zone>/chrono), the controller's view */
struct ChronoState
{
    bool        on = false;
    bool        paused = false;             /* house mode away */
    bool        manual = false;             /* manual setpoint until the next change */
    QString     profile;                    /* "weekday" / "holiday", empty if unknown */
    bool        hasNext = false;            /* no next change while the controller clock is not set */
    QString     nextChange;                 /* "22:30", controller local time */
    double      nextSetpoint = 0;
};

/* A slot occurrence: setpoint and when it begins (local time) */
struct ChronoPoint
{
    double      temp = 0;
    QDateTime   start;                      /* invalid: no chrono */

    bool isValid() const { return start.isValid(); }
};

namespace Chrono
{
    bool isHoliday(const QDate &date, const ChronoConfig &config);
    /* Profile of that day (the weekday one if the holiday one is empty) */
    const QVector<ChronoSlot> &profile(const QDate &date, const ChronoConfig &config);
    /* "weekday" or "holiday": profile used that day */
    QString profileName(const QDate &date, const ChronoConfig &config);

    /* Slot in force at now: the last one begun today, before the first
     * slot of the day the last one of the previous day. Invalid if the
     * chrono is off. */
    ChronoPoint current(const QDateTime &now, const ChronoConfig &config);
    /* Next slot after now. Invalid if the chrono is off. */
    ChronoPoint next(const QDateTime &now, const ChronoConfig &config);
    /* Setpoint of each stepMin interval of that day, from 00:00 (96 with
     * 15 minutes). Empty if the profiles are empty; ignores enabled (the
     * page also shows a program that is off). */
    QVector<double> dayTemps(const QDate &date, const ChronoConfig &config, int stepMin = 15);

    /* Editing steps, false if not possible: time of slot k to the next
     * (dir > 0) or previous EDIT_STEP_MIN point, strictly between its
     * neighbours (the order never changes) */
    constexpr int EDIT_STEP_MIN = 15;
    bool stepTime(QVector<ChronoSlot> &list, int k, int dir);
    /* setpoint of slot k +/- 0.5, within 5..25 */
    bool stepTemp(QVector<ChronoSlot> &list, int k, int dir);
    /* Split slot k in two at the middle of its interval (until the next
     * slot, or midnight for the last one), on the EDIT_STEP_MIN grid, the
     * new slot with the same setpoint at k + 1. Empty list: one slot
     * 08:00 20.0 at 0. Returns the index of the new slot, -1 if not
     * possible (MAX_SLOTS, interval shorter than two steps). */
    int splitSlot(QVector<ChronoSlot> &list, int k);

    /* <zone>/chrono/profile JSON into config; false (config untouched) if
     * it is not a program */
    bool parseProgram(const QByteArray &payload, ChronoConfig &config);
    /* <zone>/chrono JSON into state; false (state untouched) if invalid */
    bool parseState(const QByteArray &payload, ChronoState &state);
    /* <zone>/chrono/profile/set JSON: enabled, weekday, holiday */
    QByteArray programCommand(const ChronoConfig &config);
    /* The same program as the controller stores it: enabled and the two
     * profiles, compared after sorting and rounding to 0.5 */
    bool sameProgram(const ChronoConfig &a, const ChronoConfig &b);

    /* "08:00=20.5, 23:00=17.0" (logs) */
    QString toString(const QVector<ChronoSlot> &profile);
}

#endif // CHRONO_H
