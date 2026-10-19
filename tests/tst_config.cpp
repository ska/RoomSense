#include <QTest>
#include "configfile.h"
#include "testutil.h"

using namespace TestUtil;

static mqtt_brk_t readMqtt(const QString &path)
{
    mqtt_brk_t m;
    memset(&m, 0, sizeof(m));
    ConfigFile(path).ReadMqttInfo(&m);
    return m;
}

static QVector<sensor_t> readSensors(const QString &path)
{
    QVector<sensor_t> v;
    ConfigFile(path).ReadSensorsInfo(v);
    return v;
}

class TstConfig : public QObject
{
    Q_OBJECT

private slots:
    void mqttDefaults()
    {
        QTemporaryDir dir;
        const mqtt_brk_t m = readMqtt(writeIni(dir, "[MQTT]\nbroker_addr=\n"));
        QCOMPARE(QString(m.address), QString("localhost"));
        QCOMPARE(int(m.port), 1883);
        QCOMPARE(int(m.publishInterval), 120);
        QCOMPARE(QString(m.uniqueId), QString());
        QCOMPARE(QString(m.uname), QString());
    }

    void mqttValues()
    {
        QTemporaryDir dir;
        const mqtt_brk_t m = readMqtt(writeIni(dir,
            "[MQTT]\nbroker_addr=broker.example\nbroker_port=8883\nbroker_uname=user\n"
            "broker_password=secret\nunique_id=gw1\npublish_interval_s=300\n"));
        QCOMPARE(QString(m.address), QString("broker.example"));
        QCOMPARE(int(m.port), 8883);
        QCOMPARE(QString(m.uname), QString("user"));
        QCOMPARE(QString(m.password), QString("secret"));
        QCOMPARE(QString(m.uniqueId), QString("gw1"));
        QCOMPARE(int(m.publishInterval), 300);
    }

    void publishIntervalInvalid_data()
    {
        QTest::addColumn<QString>("value");
        QTest::newRow("too short") << "59";
        QTest::newRow("too long")  << "301";
        QTest::newRow("not a number") << "abc";
    }

    void publishIntervalInvalid()
    {
        QFETCH(QString, value);
        QTemporaryDir dir;
        QCOMPARE(int(readMqtt(writeIni(dir, "[MQTT]\npublish_interval_s=" + value + "\n")).publishInterval), 120);
    }

    void longValueTruncated()
    {
        QTemporaryDir dir;
        const mqtt_brk_t m = readMqtt(writeIni(dir, "[MQTT]\nbroker_password=" + QString(200, 'p') + "\n"));
        QCOMPARE(int(strlen(m.password)), int(sizeof(m.password)) - 1);
    }

    void sensorMac()
    {
        QTemporaryDir dir;
        const auto v = readSensors(writeIni(dir,
            "[SENSORS]\n"
            "a\\mac=a4:c1:38:0a:0b:0c\n"
            "b\\mac= A4:C1:38:0D:0E:0F \n"
            "c\\mac=A4-C1-38-00-00-01\n"));
        QCOMPARE(v.size(), 3);
        QCOMPARE(QString(v[0].smac), QString("A4:C1:38:0A:0B:0C"));    /* lower case: matched */
        QCOMPARE(QString(v[1].smac), QString("A4:C1:38:0D:0E:0F"));
        QCOMPARE(QString(v[2].smac), QString());                        /* invalid: never read */
    }

    void sensorZone()
    {
        QTemporaryDir dir;
        const auto v = readSensors(writeIni(dir,
            "[SENSORS]\n"
            "a1\\name=salotto\n"
            "a2\\name=entrance\na2\\zone=ingresso\n"
            "a3\\name=x\na3\\zone=salotto\n"          /* duplicate of a1 */
            "a4\\name=y\na4\\zone=sala pranzo\n"      /* invalid */
            "a5\\name=list\n"                         /* reserved by AcquaThermoNet */
            "a6\\name=" + QString(40, 'n') + "\n"      /* name truncated, zone from it */
            "a7\\name=z\na7\\zone=" + QString(32, 'z') + "\n"     /* zone too long */
            "a8\\name=mode\n"));                     /* reserved: house mode topics */
        QCOMPARE(v.size(), 8);
        QCOMPARE(QString(v[0].zone), QString("salotto"));
        QCOMPARE(QString(v[1].zone), QString("ingresso"));
        QCOMPARE(QString(v[2].zone), QString());
        QCOMPARE(QString(v[3].zone), QString());
        QCOMPARE(QString(v[4].zone), QString());
        QCOMPARE(QString(v[5].name), QString(31, 'n'));
        QCOMPARE(QString(v[5].zone), QString(31, 'n'));
        QCOMPARE(QString(v[6].zone), QString());
        QCOMPARE(QString(v[7].zone), QString());
        for(int i = 0; i < v.size(); i++)
            QCOMPARE(int(v[i].id), i);
    }

    void sensorHeating()
    {
        QTemporaryDir dir;
        const auto v = readSensors(writeIni(dir,
            "[SENSORS]\n"
            "a1\\name=salotto\n"                                 /* default: heating */
            "a2\\name=camera\na2\\heating=true\n"
            "a3\\name=taverna\na3\\heating=false\n"));
        QCOMPARE(v.size(), 3);
        QVERIFY(!v[0].noHeating);
        QVERIFY(!v[1].noHeating);
        QVERIFY(v[2].noHeating);
        QCOMPARE(QString(v[2].zone), QString("taverna"));      /* still published */
    }

    void ensureSettings()
    {
        QTemporaryDir dir;
        const QString tpl = writeIni(dir, "[MQTT]\nbroker_port=1884\n", "setting.default.ini");
        QFile::setPermissions(tpl, QFileDevice::ReadOwner);
        const QString ini = dir.filePath("setting.ini");

        QVERIFY(ConfigFile::ensureSettings(ini, tpl));
        QVERIFY(QFile::exists(ini));
        QVERIFY(QFile::permissions(ini) & QFileDevice::WriteOwner);    /* edited by hand */
        QCOMPARE(int(readMqtt(ini).port), 1884);

        /* never overwritten */
        writeIni(dir, "[MQTT]\n; edited by hand\nbroker_port=1885\n");   /* other size: QSettings cache */
        QVERIFY(ConfigFile::ensureSettings(ini, tpl));
        QCOMPARE(int(readMqtt(ini).port), 1885);

        /* no template: false, the defaults are used */
        QTemporaryDir empty;
        QVERIFY(!ConfigFile::ensureSettings(empty.filePath("setting.ini"), empty.filePath("setting.default.ini")));
    }

    void logInfo()
    {
        QTemporaryDir dir;
        QString path;
        qint64 maxBytes;
        int files;

        ConfigFile(writeIni(dir, "[LOG]\nfile=log/app.log\nmax_kb=100\n")).ReadLogInfo(path, maxBytes, files);
        QCOMPARE(path, dir.filePath("log/app.log"));               /* relative to setting.ini */
        QCOMPARE(maxBytes, qint64(100 * 1024));
        QCOMPARE(files, 3);

        ConfigFile(writeIni(dir, "[LOG]\nfile=\n")).ReadLogInfo(path, maxBytes, files);
        QVERIFY(path.isEmpty());                                   /* no file */
    }

    void screenSaver()
    {
        QTemporaryDir dir;
        screensaver_t ss = ConfigFile(writeIni(dir, "[SCREENSAVER]\nidle_s=0\n"
                                               "webcam_url=http://example.com/cam.jpg\nwebcam_refresh_s=120\n")).ReadScreenSaverInfo();
        QCOMPARE(ss.idleS, 0);                              /* off */
        QCOMPARE(ss.webcamUrl, QString("http://example.com/cam.jpg"));
        QCOMPARE(ss.webcamRefreshS, 120);

        QTemporaryDir dir2;
        ss = ConfigFile(writeIni(dir2, "[SCREENSAVER]\nidle_s=5\n"
                                       "webcam_url=ftp://example.com/x\nwebcam_refresh_s=x\n")).ReadScreenSaverInfo();
        QCOMPARE(ss.idleS, 30);
        QCOMPARE(ss.webcamUrl, QString());
        QCOMPARE(ss.webcamRefreshS, 60);
    }

    void weather()
    {
        QTemporaryDir dir;
        weather_cfg_t wc = ConfigFile(writeIni(dir, "[WEATHER]\nprovider=metno\nlat=45.1\nlon=12.3\n"
                                                    "altitude=10\ncontact=info@mail.com\npoll_s=900\n")).ReadWeatherInfo();
        QVERIFY(wc.enabled());
        QCOMPARE(wc.lat, 45.1);
        QCOMPARE(wc.lon, 12.3);
        QCOMPARE(wc.altitude, 10);
        QCOMPARE(wc.contact, QString("info@mail.com"));
        QCOMPARE(wc.pollS, 900);

        QTemporaryDir dir2;
        wc = ConfigFile(writeIni(dir2, "[WEATHER]\nprovider=wttr\nlocation=home\npoll_s=10\n")).ReadWeatherInfo();
        QVERIFY(wc.enabled());
        QCOMPARE(wc.provider, weather_cfg_t::Wttr);
        QCOMPARE(wc.pollS, 600);                            /* at least 60 */

        QTemporaryDir dir3;
        wc = ConfigFile(writeIni(dir3, "[WEATHER]\nprovider=other\nlat=45\n")).ReadWeatherInfo();
        QCOMPARE(wc.provider, weather_cfg_t::MetNo);
        QVERIFY(!wc.enabled());                             /* lon missing */
    }

    void settingTemplate()
    {
        /* the deployed template: placeholders, five zones, valid */
        const QString tpl = QFINDTESTDATA("../setting.default.ini");
        QVERIFY(!tpl.isEmpty());
        const mqtt_brk_t m = readMqtt(tpl);
        QCOMPARE(QString(m.address), QString("localhost"));
        QCOMPARE(int(m.publishInterval), 120);
        QString log;
        qint64 maxBytes;
        int files;
        ConfigFile(tpl).ReadLogInfo(log, maxBytes, files);
        QVERIFY(log.endsWith("/log/RoomSense.log"));
        QCOMPARE(maxBytes, qint64(512 * 1024));
        QCOMPARE(files, 3);
        const screensaver_t ss = ConfigFile(tpl).ReadScreenSaverInfo();
        QCOMPARE(ss.idleS, 30);
        QCOMPARE(ss.webcamUrl, QString());                  /* no place name in the template */
        QCOMPARE(ss.webcamRefreshS, 60);
        const weather_cfg_t wc = ConfigFile(tpl).ReadWeatherInfo();
        QVERIFY(!wc.enabled());                             /* no coordinates in the template */
        QCOMPARE(wc.provider, weather_cfg_t::MetNo);
        QCOMPARE(wc.contact, QString());
        QCOMPARE(wc.pollS, 600);
        const auto v = readSensors(tpl);
        QCOMPARE(v.size(), 5);
        for(const sensor_t &s : v)
        {
            QVERIFY(QString(s.smac).startsWith("A4:C1:38:00:00:"));
            QCOMPARE(QString(s.zone), QString(s.name));
        }
    }
};

int runConfigTests(int argc, char *argv[])
{
    TstConfig t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_config.moc"
