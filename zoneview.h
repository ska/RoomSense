#ifndef ZONEVIEW_H
#define ZONEVIEW_H

#include <QString>
#include "zonecontrol.h"

/*
 * What the main page shows for one zone, from its last reading and the
 * AcquaThermoNet state (ZoneControl). No widgets here: ZoneCard only
 * displays it, the rules are unit tested.
 */
namespace ZoneView
{
    /* Sensor not heard for longer: stale, then lost (AcquaThermoNet closes
     * the zone after 15 min without readings) */
    const qint64 STALE_S = 5 * 60;
    const qint64 LOST_S  = 15 * 60;

    enum Level   { Ok, Stale, Lost, Never };
    enum Heat    { HeatUnknown, HeatOn, HeatIdle, HeatNoSensor };
    enum SetPoint{ Confirmed, Pending, LastKnown, NoSetPoint, NoZone, InMode, InBoost };

    typedef struct {
        bool    hasZone = true;         /*the sensor has a valid zone*/
        bool    heard = false;          /*a reading arrived since the start*/
        qint64  ageS = 0;               /*seconds since the last reading*/
        int     temp = 0;               /*0.1 degC*/
        int     hum = 0;                /*%, 0 = unknown*/
        int     batt = 0;               /*%, 0 = unknown*/
        ZoneControl::zone_t control;
        bool    controllerOnline = false;
        ZoneControl::HouseMode houseMode = ZoneControl::Normal;
    } input_t;

    typedef struct {
        QString  tempText;              /*"21.3°", "--.-°"*/
        bool     tempMuted;
        Heat     heat;
        QString  heatText;
        SetPoint setPoint;
        QString  setPointText;          /*"21.5", "--.-"*/
        QString  setPointLabel;         /*"setpoint", "sending…", ...*/
        QString  humText;
        QString  battText;
        bool     battLow;
        Level    level;
        QString  statusText;
    } view_t;

    view_t make(const input_t &in);

    /* House mode button in the header of the main page */
    typedef struct {
        bool    hasMode = false;        /*a mode/state was received*/
        ZoneControl::HouseMode mode = ZoneControl::Normal;
        bool    pending = false;        /*requested, not confirmed yet*/
        ZoneControl::HouseMode pendingMode = ZoneControl::Normal;
        int     remainingS = 0;         /*window, boost: seconds left*/
    } mode_input_t;

    typedef struct {
        QString text;                   /*"Normal", "Windows open", "Sending…", ...*/
        QString detail;                 /*"23 min left" (window, boost), else empty*/
        QString icon;                   /*"home", "window", "plane", "flame"*/
        QString level;                  /*stylesheet: unknown, pending, normal, window, away, boost*/
    } mode_view_t;

    mode_view_t modeButton(const mode_input_t &in);

    /* "30 s", "7 min", "3 h" */
    QString age(qint64 seconds);
}

#endif // ZONEVIEW_H
