#ifndef DIAGVIEW_H
#define DIAGVIEW_H

#include <QString>
#include <QVector>
#include "zoneview.h"

/*
 * What the diagnostic page shows: the state of the gateway (broker,
 * controller, BLE adapter, watchdog, log) and one row per sensor. No
 * widgets here: DiagPage only displays it, the rules are unit tested.
 */
namespace DiagView
{
    /* Color of a state: the dot and the value */
    enum Level { Ok, Warn, Bad, Muted };

    /* Last advertisement older than this: warning, then error (the BLE
     * thread restarts the scan after 120 s without any) */
    const qint64 ADV_WARN_S = 30;
    const qint64 ADV_BAD_S  = 120;

    typedef struct {
        QString label;
        QString value;
        Level   level;
    } row_t;

    typedef struct {
        int     brokerState = 0;        /*QMqttClient::ClientState*/
        QString brokerHost;             /*empty: localhost*/
        int     brokerPort = 0;
        QString clientId;
        bool    controllerOnline = false;
        int     bleState = 0;           /*BleStatus::State*/
        int     bleDevice = -1;         /*hci<N> while scanning*/
        qint64  lastAdvAgeS = -1;       /*-1: none yet*/
        bool    watchdogActive = false;
        int     watchdogTimeoutS = 0;
        qint64  uptimeS = 0;
        QString logFile;                /*as shown; empty: no log file*/
        qint64  logBytes = -1;          /*-1: the file is missing*/
        bool    weatherEnabled = false;
        QString weatherSource;          /*"met.no", "wttr.in"*/
        qint64  weatherAgeS = -1;       /*since the last good data, -1: none*/
        QString weatherError;           /*of the last request, empty: none*/
    } system_t;

    /* MQTT broker, client id, heating controller, BLE adapter, last
     * advertisement, watchdog, uptime, log file, weather */
    QVector<row_t> system(const system_t &in);

    typedef struct {
        QString  name;
        QString  mac;
        bool     heard = false;         /*a reading arrived since the start*/
        qint64   ageS = 0;              /*seconds since the last reading*/
        unsigned readTime = 0;          /*epoch s of the last reading*/
        int      rssi = 0;              /*dBm*/
        quint32  sent = 0;              /*messages acknowledged by the broker*/
        unsigned sentTime = 0;          /*epoch s of the last one, 0 = none*/
        quint32  filtered = 0;          /*readings dropped as implausible*/
        unsigned now = 0;               /*epoch s, for the time format*/
    } sensor_in_t;

    typedef struct {
        QString  name;
        QString  mac;
        QString  rssi;                  /*"-62 dBm", "--" when not heard*/
        QString  read;                  /*"12:34:58", "never"*/
        QString  sent;                  /*"412, 12:34:10", "0"*/
        QString  filtered;              /*"0", "1 dropped"*/
        ZoneView::Level level;          /*as the main page: ok, stale, lost, never*/
    } sensor_view_t;

    sensor_view_t sensor(const sensor_in_t &in);

    /* "45 s", "12 min", "4 h 12 min", "3 d 4 h" */
    QString uptime(qint64 seconds);
    /* "512 B", "212 KB", "1.4 MB" */
    QString size(qint64 bytes);
    /* "12:34:58" today (or within a day), "28 Sep 12:34" before */
    QString clock(unsigned epoch, unsigned now);
}

#endif // DIAGVIEW_H
