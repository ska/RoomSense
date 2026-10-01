#include <QDateTime>
#include <QTest>
#include "blestatus.h"
#include "diagview.h"

using namespace DiagView;

/* Everything fine: connected, scanning, watchdog on, log file present */
static system_t healthy()
{
    system_t in;
    in.brokerState = 2;
    in.brokerHost = "broker.lan";
    in.brokerPort = 1883;
    in.clientId = "RoomSense-panel";
    in.controllerOnline = true;
    in.bleState = BleStatus::Scanning;
    in.bleDevice = 0;
    in.lastAdvAgeS = 2;
    in.watchdogActive = true;
    in.watchdogTimeoutS = 60;
    in.uptimeS = 3 * 86400 + 4 * 3600 + 120;
    in.logFile = "log/RoomSense.log";
    in.logBytes = 212 * 1024 + 100;
    in.weatherEnabled = true;
    in.weatherSource = "met.no";
    in.weatherAgeS = 12 * 60;
    return in;
}

static row_t row(const QVector<row_t> &rows, const QString &label)
{
    for(const row_t &r : rows)
        if(r.label == label)
            return r;
    return { "missing", "missing", Muted };
}

class TstDiagView : public QObject
{
    Q_OBJECT

private slots:
    void formats()
    {
        QCOMPARE(uptime(45), QString("45 s"));
        QCOMPARE(uptime(12 * 60 + 5), QString("12 min"));
        QCOMPARE(uptime(4 * 3600 + 12 * 60), QString("4 h 12 min"));
        QCOMPARE(uptime(3 * 86400 + 4 * 3600 + 59), QString("3 d 4 h"));
        QCOMPARE(size(512), QString("512 B"));
        QCOMPARE(size(212 * 1024 + 1), QString("212 KB"));
        QCOMPARE(size(1468006), QString("1.4 MB"));

        const unsigned now = 1790000000;
        QCOMPARE(clock(now - 60, now), QDateTime::fromSecsSinceEpoch(now - 60).toString("HH:mm:ss"));
        QCOMPARE(clock(now - 3 * 86400, now),
                 QLocale(QLocale::English).toString(QDateTime::fromSecsSinceEpoch(now - 3 * 86400), "d MMM HH:mm"));
    }

    void systemHealthy()
    {
        const QVector<row_t> rows = system(healthy());
        QCOMPARE(rows.size(), 9);
        for(const row_t &r : rows)
            QVERIFY2(r.level == Ok, qPrintable(r.label));
        QCOMPARE(row(rows, "MQTT broker").value, QString("connected, broker.lan:1883"));
        QCOMPARE(row(rows, "Client id").value, QString("RoomSense-panel"));
        QCOMPARE(row(rows, "Heating controller").value, QString("online"));
        QCOMPARE(row(rows, "BLE adapter").value, QString("hci0, scanning"));
        QCOMPARE(row(rows, "Last advertisement").value, QString("2 s ago"));
        QCOMPARE(row(rows, "Watchdog").value, QString("active, 60 s"));
        QCOMPARE(row(rows, "Uptime").value, QString("3 d 4 h"));
        QCOMPARE(row(rows, "Log file").value, QString("log/RoomSense.log, 212 KB"));
        QCOMPARE(row(rows, "Weather").value, QString("met.no, 12 min ago"));
    }

    void systemWeather()
    {
        system_t in = healthy();
        in.weatherError = "TLS initialization failed";
        QCOMPARE(row(system(in), "Weather").value, QString("met.no: TLS initialization failed"));
        QCOMPARE(row(system(in), "Weather").level, Bad);
        in.weatherError.clear();
        in.weatherAgeS = -1;
        QCOMPARE(row(system(in), "Weather").value, QString("met.no, waiting"));
        QCOMPARE(row(system(in), "Weather").level, Warn);
        in.weatherEnabled = false;
        QCOMPARE(row(system(in), "Weather").value, QString("off"));
        QCOMPARE(row(system(in), "Weather").level, Muted);
    }

    void systemProblems()
    {
        system_t in = healthy();
        in.brokerState = 0;
        in.brokerHost = QString();
        in.controllerOnline = false;
        in.bleState = BleStatus::Retrying;
        in.lastAdvAgeS = 121;
        in.watchdogActive = false;
        in.logBytes = -1;
        QVector<row_t> rows = system(in);
        QCOMPARE(row(rows, "MQTT broker").value, QString("disconnected, localhost:1883"));
        QCOMPARE(row(rows, "MQTT broker").level, Bad);
        QCOMPARE(row(rows, "Heating controller").level, Bad);
        QCOMPARE(row(rows, "BLE adapter").value, QString("error, retrying"));
        QCOMPARE(row(rows, "BLE adapter").level, Bad);
        QCOMPARE(row(rows, "Last advertisement").value, QString("2 min ago"));
        QCOMPARE(row(rows, "Last advertisement").level, Bad);
        QCOMPARE(row(rows, "Watchdog").value, QString("not active"));
        QCOMPARE(row(rows, "Watchdog").level, Warn);
        QCOMPARE(row(rows, "Log file").value, QString("log/RoomSense.log, missing"));
        QCOMPARE(row(rows, "Log file").level, Bad);

        in.brokerState = 1;
        in.bleState = BleStatus::Starting;
        in.lastAdvAgeS = -1;
        in.logFile = QString();
        rows = system(in);
        QCOMPARE(row(rows, "MQTT broker").level, Warn);
        QCOMPARE(row(rows, "BLE adapter").value, QString("starting"));
        QCOMPARE(row(rows, "BLE adapter").level, Warn);
        QCOMPARE(row(rows, "Last advertisement").value, QString("none yet"));
        QCOMPARE(row(rows, "Last advertisement").level, Warn);
        QCOMPARE(row(rows, "Log file").value, QString("none"));
        QCOMPARE(row(rows, "Log file").level, Muted);

        in.lastAdvAgeS = 31;
        QCOMPARE(row(system(in), "Last advertisement").level, Warn);
        in.bleState = BleStatus::Simulated;
        QCOMPARE(row(system(in), "BLE adapter").value, QString("simulated (desktop)"));
    }

    void sensorRows()
    {
        sensor_in_t in;
        in.name = "Salotto";
        in.mac = "A4:C1:38:00:00:01";
        in.now = 1790000000;

        sensor_view_t v = sensor(in);                          /* never heard */
        QCOMPARE(v.level, ZoneView::Never);
        QCOMPARE(v.rssi, QString("--"));
        QCOMPARE(v.read, QString("never"));
        QCOMPARE(v.sent, QString("0"));
        QCOMPARE(v.filtered, QString("0"));

        in.heard = true;
        in.ageS = 4;
        in.readTime = in.now - 4;
        in.rssi = -62;
        in.sent = 412;
        in.sentTime = in.now - 50;
        in.filtered = 1;
        v = sensor(in);
        QCOMPARE(v.level, ZoneView::Ok);
        QCOMPARE(v.name, QString("Salotto"));
        QCOMPARE(v.mac, QString("A4:C1:38:00:00:01"));
        QCOMPARE(v.rssi, QString("-62 dBm"));
        QCOMPARE(v.read, clock(in.now - 4, in.now));
        QCOMPARE(v.sent, QString("412, ") + clock(in.now - 50, in.now));
        QCOMPARE(v.filtered, QString("1 dropped"));

        in.ageS = ZoneView::STALE_S + 1;
        QCOMPARE(sensor(in).level, ZoneView::Stale);
        QCOMPARE(sensor(in).rssi, QString("-62 dBm"));
        in.ageS = ZoneView::LOST_S + 1;                       /* lost: no RSSI, time kept */
        QCOMPARE(sensor(in).level, ZoneView::Lost);
        QCOMPARE(sensor(in).rssi, QString("--"));
        QCOMPARE(sensor(in).read, clock(in.now - 4, in.now));
    }
};

int runDiagViewTests(int argc, char *argv[])
{
    TstDiagView t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_diagview.moc"
