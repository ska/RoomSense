#include "diagview.h"
#include "blestatus.h"
#include <QDateTime>
#include <QLocale>

QString DiagView::uptime(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    if(seconds < 60)
        return QString("%1 s").arg(seconds);
    if(seconds < 3600)
        return QString("%1 min").arg(seconds / 60);
    if(seconds < 86400)
        return QString("%1 h %2 min").arg(seconds / 3600).arg(seconds % 3600 / 60);
    return QString("%1 d %2 h").arg(seconds / 86400).arg(seconds % 86400 / 3600);
}

QString DiagView::size(qint64 bytes)
{
    if(bytes < 1024)
        return QString("%1 B").arg(bytes);
    if(bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString DiagView::clock(unsigned epoch, unsigned now)
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(epoch);
    if(epoch + 86400 > now)
        return t.toString("HH:mm:ss");
    return QLocale(QLocale::English).toString(t, "d MMM HH:mm");
}

QVector<DiagView::row_t> DiagView::system(const system_t &in)
{
    QVector<row_t> rows;

    /* MQTT */
    const QString broker = QString("%1:%2").arg(in.brokerHost.isEmpty() ? QString("localhost") : in.brokerHost)
                                           .arg(in.brokerPort);
    switch(in.brokerState)
    {
    case 2:  rows.append({ "MQTT broker", "connected, " + broker, Ok }); break;
    case 1:  rows.append({ "MQTT broker", "connecting, " + broker, Warn }); break;
    default: rows.append({ "MQTT broker", "disconnected, " + broker, Bad }); break;
    }
    rows.append({ "Client id", in.clientId, Ok });
    rows.append({ "Heating controller", in.controllerOnline ? "online" : "offline", in.controllerOnline ? Ok : Bad });

    /* BLE */
    switch(in.bleState)
    {
    case BleStatus::Scanning:
        rows.append({ "BLE adapter", QString("hci%1, scanning").arg(in.bleDevice), Ok });
        break;
    case BleStatus::Retrying:
        rows.append({ "BLE adapter", "error, retrying", Bad });
        break;
    case BleStatus::Simulated:
        rows.append({ "BLE adapter", "simulated (desktop)", Ok });
        break;
    default:
        rows.append({ "BLE adapter", "starting", Warn });
        break;
    }
    if(in.lastAdvAgeS < 0)
        rows.append({ "Last advertisement", "none yet", Warn });
    else
        rows.append({ "Last advertisement", ZoneView::age(in.lastAdvAgeS) + " ago",
                      in.lastAdvAgeS > ADV_BAD_S ? Bad : in.lastAdvAgeS > ADV_WARN_S ? Warn : Ok });

    /* device */
    if(in.watchdogActive)
        rows.append({ "Watchdog", QString("active, %1 s").arg(in.watchdogTimeoutS), Ok });
    else
        rows.append({ "Watchdog", "not active", Warn });
    rows.append({ "Uptime", uptime(in.uptimeS), Ok });
    if(in.logFile.isEmpty())
        rows.append({ "Log file", "none", Muted });
    else if(in.logBytes < 0)
        rows.append({ "Log file", in.logFile + ", missing", Bad });
    else
        rows.append({ "Log file", in.logFile + ", " + size(in.logBytes), Ok });

    /* weather: a failing request shows its error (e.g. no OpenSSL) */
    if(!in.weatherEnabled)
        rows.append({ "Weather", "off", Muted });
    else if(!in.weatherError.isEmpty())
        rows.append({ "Weather", in.weatherSource + ": " + in.weatherError, Bad });
    else if(in.weatherAgeS < 0)
        rows.append({ "Weather", in.weatherSource + ", waiting", Warn });
    else
        rows.append({ "Weather", QString("%1, %2 ago").arg(in.weatherSource, uptime(in.weatherAgeS)), Ok });
    return rows;
}

DiagView::sensor_view_t DiagView::sensor(const sensor_in_t &in)
{
    sensor_view_t v;
    v.name = in.name;
    v.mac = in.mac;

    if(!in.heard)
        v.level = ZoneView::Never;
    else if(in.ageS > ZoneView::LOST_S)
        v.level = ZoneView::Lost;
    else if(in.ageS > ZoneView::STALE_S)
        v.level = ZoneView::Stale;
    else
        v.level = ZoneView::Ok;

    const bool current = (v.level == ZoneView::Ok || v.level == ZoneView::Stale);
    v.rssi = current ? QString("%1 dBm").arg(in.rssi) : QString("--");
    v.read = in.heard ? clock(in.readTime, in.now) : QString("never");
    v.sent = in.sent == 0 ? QString("0") : QString("%1, %2").arg(in.sent).arg(clock(in.sentTime, in.now));
    v.filtered = in.filtered == 0 ? QString("0") : QString("%1 dropped").arg(in.filtered);
    return v;
}
