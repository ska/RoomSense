#ifndef SAVERVIEW_H
#define SAVERVIEW_H

#include <QString>
#include <QStringList>
#include <QVector>
#include "weather.h"
#include "zoneview.h"

/*
 * Summary at the bottom of the screen saver: alarms first (controller or
 * network offline, sensors lost or never heard, low batteries), then the
 * house mode; the rooms heating apart (shown as icons). No widgets here:
 * unit tested.
 */
namespace SaverView
{
    typedef struct {
        QString name;
        bool    hasZone = true;
        ZoneView::Level level = ZoneView::Never;
        bool    heat = false;               /*state_mode heat*/
        int     batt = 0;                   /*%, 0 = unknown*/
    } zone_t;

    typedef struct {
        QVector<zone_t> zones;
        bool    brokerConnected = false;
        bool    controllerOnline = false;
        ZoneView::mode_view_t mode;         /*house mode, as the header button*/
    } input_t;

    enum Level { Ok, Info, Alarm };

    typedef struct {
        QString text;
        Level   level;
    } line_t;

    QVector<line_t> summary(const input_t &in);
    /* Names of the rooms heating now; empty also when not known (controller
     * or network offline): see heatingKnown */
    QStringList heating(const input_t &in);
    bool heatingKnown(const input_t &in);

    /* Weather under the clock: hidden without data or when older than
     * WEATHER_MAX_AGE_S (no request working for hours) */
    const qint64 WEATHER_MAX_AGE_S = 3 * 3600;

    typedef struct {
        bool    shown = false;
        QString icon;                       /*"wx-sun", ...; empty: none (wttr.in)*/
        QString temp;                       /*"-2.6°"*/
        QString details;                    /*"Humidity 71 % · Wind 3.2 m/s · Rain 0.4 mm"*/
    } weather_view_t;

    /* ageS: seconds since the data arrived, < 0: no data */
    weather_view_t weather(const weather_t &w, qint64 ageS);
    /* met.no symbol_code to one of the wx-* icons, empty if unknown */
    QString weatherIcon(const QString &symbol);
}

#endif // SAVERVIEW_H
