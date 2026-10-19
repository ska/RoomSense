#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include "fakebroker.h"
#include "monoclock.h"
#include "mqtt.h"

#define TOPIC_SALOTTO   "RoomSense/salotto/data"
#define TOPIC_INGRESSO  "RoomSense/ingresso/data"
#define TOPIC_STATUS    "RoomSense/status"
#define CTRL_STATUS     "AcquaThermoNet/status"
#define SALOTTO_STATE   "AcquaThermoNet/salotto/state_temp"
#define SALOTTO_SET     "AcquaThermoNet/salotto/set_temp"
#define MODE_STATE      "AcquaThermoNet/mode/state"
#define MODE_SET        "AcquaThermoNet/mode/set"
#define SALOTTO_CHRONO  "AcquaThermoNet/salotto/chrono"
#define SALOTTO_PROFILE "AcquaThermoNet/salotto/chrono/profile"
#define SALOTTO_CHRONO_SET  "AcquaThermoNet/salotto/chrono/set"
#define SALOTTO_PROFILE_SET "AcquaThermoNet/salotto/chrono/profile/set"
#define PROGRAM_OFF     R"({"edited":false,"enabled":false,"holiday":[],"holiday_days":[6,7],"weekday":[{"at":"06:30","temp":20.5}]})"

static sensor_env_t reading(int temp, int hum = 48, int batt = 85, int mv = 2950)
{
    sensor_env_t e;
    memset(&e, 0, sizeof(e));
    e.temp = temp;
    e.hum = hum;
    e.batt = batt;
    e.battmv = mv;
    e.ldt = 1790000000;
    return e;
}

static void addSensor(QVector<sensor_t> &v, const char *mac, const char *zone)
{
    sensor_t s;
    memset(&s, 0, sizeof(s));
    s.id = v.size();
    qstrncpy(s.smac, mac, sizeof(s.smac));
    qstrncpy(s.name, zone, sizeof(s.name));
    qstrncpy(s.zone, zone, sizeof(s.zone));
    v.append(s);
}

static QJsonObject json(const FakeBroker::publish_t &p)
{
    return QJsonDocument::fromJson(p.payload).object();
}

/* Processes events for ms (nothing expected to happen) */
static void idle(int ms = 300)
{
    QTest::qWait(ms);
}

class TstMqtt : public QObject
{
    Q_OBJECT

private:
    FakeBroker         *m_broker = nullptr;
    QVector<sensor_t>   m_sensors;
    mqtt_brk_t          m_mbi;
    Mqtt               *m_mqtt = nullptr;

    /* Next reading of sensor index, after moving the clock by advanceS */
    void send(int index, const sensor_env_t &e, int advanceS = 0)
    {
        MonoClock::advanceForTest(qint64(advanceS) * 1000);
        m_mqtt->sensorValueIsChanged(index, e.ldt, e);
    }

    /* First reading of a sensor: published once a later one, 10 s after
     * and within 0.5 degC, confirms it */
    void first(int index, const sensor_env_t &e)
    {
        send(index, e);
        send(index, e, 10);
    }

    int count(const char *topic) const { return m_broker->on(topic).size(); }

private slots:
    void init()
    {
        m_broker = new FakeBroker;
        QVERIFY(m_broker->listen());
        /* AcquaThermoNet already running: its retained state */
        m_broker->deliver(CTRL_STATUS, "online", true);
        m_broker->deliver(SALOTTO_STATE, "20", true);
        m_broker->deliver("AcquaThermoNet/salotto/state_mode", "heat", true);

        m_sensors.clear();
        addSensor(m_sensors, "A4:C1:38:00:00:01", "salotto");
        addSensor(m_sensors, "A4:C1:38:00:00:02", "ingresso");
        addSensor(m_sensors, "A4:C1:38:00:00:03", "");         /* no zone */
        addSensor(m_sensors, "A4:C1:38:00:00:04", "taverna");  /* outside the heating */
        m_sensors[3].noHeating = true;

        memset(&m_mbi, 0, sizeof(m_mbi));
        qstrncpy(m_mbi.address, "127.0.0.1", sizeof(m_mbi.address));
        m_mbi.port = m_broker->port();
        qstrncpy(m_mbi.uniqueId, "gw1", sizeof(m_mbi.uniqueId));
        m_mbi.publishInterval = 60;

        m_mqtt = new Mqtt(&m_mbi, &m_sensors);
        QTRY_COMPARE(count(TOPIC_STATUS), 1);                  /* online */
        QTRY_COMPARE(m_broker->subscriptions.size(), 7);
    }

    void cleanup()
    {
        delete m_mqtt;
        m_mqtt = nullptr;
        delete m_broker;
        m_broker = nullptr;
    }

    void connectAndStatus()
    {
        QCOMPARE(m_broker->connects.size(), 1);
        const FakeBroker::connect_t c = m_broker->connects.first();
        QCOMPARE(c.clientId, QString("RoomSense-gw1"));
        QCOMPARE(c.keepAlive, 30);
        QCOMPARE(c.willTopic, QString(TOPIC_STATUS));
        QCOMPARE(c.willMessage, QByteArray("offline"));
        QCOMPARE(c.willQos, 1);
        QVERIFY(c.willRetain);

        const FakeBroker::publish_t online = m_broker->on(TOPIC_STATUS).first();
        QCOMPARE(online.payload, QByteArray("online"));
        QVERIFY(online.retain);
    }

    void payload()
    {
        first(0, reading(204));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        const FakeBroker::publish_t p = m_broker->on(TOPIC_SALOTTO).first();
        QVERIFY(!p.retain);
        QCOMPARE(p.qos, 1);

        const QJsonObject o = json(p);
        QVERIFY(o.value("temperature").isDouble());            /* JSON number */
        QCOMPARE(o.value("temperature").toDouble(), 20.4);
        QCOMPARE(o.value("humidity").toInt(), 48);
        QCOMPARE(o.value("battery").toInt(), 85);
        QCOMPARE(o.value("battmv").toInt(), 2950);
        QCOMPARE(o.value("data_time").toVariant().toLongLong(), 1790000000LL);
        QCOMPARE(o.value("mac").toString(), QString("A4:C1:38:00:00:01"));
        QCOMPARE(o.keys().size(), 6);

        /* last sent time and counter on the broker acknowledgement */
        QTRY_COMPARE(m_mqtt->statsSentData().at(0), quint32(1));
        QVERIFY(m_sensors.at(0).ldst > 0);
    }

    void negativeTemperature()
    {
        first(0, reading(-15));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).first()).value("temperature").toDouble(), -1.5);
    }

    void unknownOmitted()
    {
        first(1, reading(200, 0, 0, 0));
        QTRY_COMPARE(count(TOPIC_INGRESSO), 1);
        const QJsonObject o = json(m_broker->on(TOPIC_INGRESSO).first());
        QVERIFY(o.contains("temperature"));
        QVERIFY(!o.contains("battery"));
        QVERIFY(!o.contains("battmv"));
        QVERIFY(!o.contains("humidity"));
    }

    void noZoneNotPublished()
    {
        send(2, reading(200));
        idle();
        for(const FakeBroker::publish_t &p : m_broker->publishes)  /* only online and discovery */
            QVERIFY2(p.topic == TOPIC_STATUS || p.topic.startsWith("homeassistant/"), qPrintable(p.topic));
    }

    void discoveryTopics()
    {
        /* only the sensor outside the heating; retained, QoS 1 */
        QStringList topics;
        for(const FakeBroker::publish_t &p : m_broker->publishes)
            if(p.topic.startsWith("homeassistant/"))
            {
                topics << p.topic;
                QCOMPARE(p.qos, 1);
                QVERIFY(p.retain);
            }
        QCOMPARE(topics, QStringList({ "homeassistant/sensor/gw1_taverna_temperature/config",
                                       "homeassistant/sensor/gw1_taverna_humidity/config",
                                       "homeassistant/sensor/gw1_taverna_battery/config" }));
    }

    void discoveryPayload()
    {
        const QJsonObject t = json(m_broker->on("homeassistant/sensor/gw1_taverna_temperature/config").first());
        QCOMPARE(t["name"].toString(), QString("Temperature"));
        QCOMPARE(t["uniq_id"].toString(), QString("gw1_taverna_temperature"));
        QCOMPARE(t["stat_t"].toString(), QString("RoomSense/taverna/data"));
        QCOMPARE(t["val_tpl"].toString(), QString("{{ value_json.temperature }}"));
        QCOMPARE(t["dev_cla"].toString(), QString("temperature"));
        QCOMPARE(t["unit_of_meas"].toString(), QString::fromUtf8("°C"));
        QCOMPARE(t["stat_cla"].toString(), QString("measurement"));
        QCOMPARE(t["avty_t"].toString(), QString(TOPIC_STATUS));
        QCOMPARE(t["exp_aft"].toInt(), 900);
        const QJsonObject d = t["device"].toObject();
        QCOMPARE(d["name"].toString(), QString("RoomSense taverna"));
        QCOMPARE(d["model"].toString(), QString("RoomSense"));
        QCOMPARE(d["sw_version"].toString(), QString(SW_VER));
        QCOMPARE(d["identifiers"].toArray(), QJsonArray({ "gw1_taverna" }));
        QVERIFY(!d["manufacturer"].toString().isEmpty());

        const QJsonObject h = json(m_broker->on("homeassistant/sensor/gw1_taverna_humidity/config").first());
        QCOMPARE(h["val_tpl"].toString(), QString("{{ value_json.humidity | default(none) }}"));
        QCOMPARE(h["dev_cla"].toString(), QString("humidity"));
        QCOMPARE(h["unit_of_meas"].toString(), QString("%"));
        QCOMPARE(h["device"].toObject(), d);
        const QJsonObject b = json(m_broker->on("homeassistant/sensor/gw1_taverna_battery/config").first());
        QCOMPARE(b["val_tpl"].toString(), QString("{{ value_json.battery | default(none) }}"));
        QCOMPARE(b["dev_cla"].toString(), QString("battery"));
        QCOMPARE(b["unit_of_meas"].toString(), QString("%"));
    }

    void discoveryNotForHeating()
    {
        QVERIFY(Mqtt::discovery("gw1", m_sensors[0]).isEmpty());   /* heating */
        sensor_t s = m_sensors[3];
        s.zone[0] = '\0';                                         /* invalid zone: not published */
        QVERIFY(Mqtt::discovery("gw1", s).isEmpty());
        QCOMPARE(Mqtt::discovery("gw1", m_sensors[3]).size(), 3);
    }

    void discoveryOnHomeAssistantOnline()
    {
        const char *topic = "homeassistant/sensor/gw1_taverna_temperature/config";
        QCOMPARE(count(topic), 1);
        m_broker->deliver("homeassistant/status", "offline");
        idle();
        QCOMPARE(count(topic), 1);
        m_broker->deliver("homeassistant/status", "online");
        QTRY_COMPARE(count(topic), 2);
        QCOMPARE(count("homeassistant/sensor/gw1_taverna_battery/config"), 2);
    }

    void discoveryAtReconnect()
    {
        const quint16 port = m_broker->port();
        m_broker->stop();
        QTRY_VERIFY(m_mqtt->isDisconnected());
        QVERIFY(m_broker->listen(port));
        QTRY_COMPARE_WITH_TIMEOUT(count("homeassistant/sensor/gw1_taverna_temperature/config"), 2, 5000);
    }

    void noHeatingPublishedNotZone()
    {
        first(3, reading(150));
        QTRY_COMPARE(count("RoomSense/taverna/data"), 1);
        QCOMPARE(json(m_broker->on("RoomSense/taverna/data").first())["temperature"].toDouble(), 15.0);

        /* no zone of ZoneControl: never a command for it */
        m_broker->deliver("AcquaThermoNet/taverna/state_temp", "20", true);
        ZoneControl *c = m_mqtt->zoneControl();
        QTRY_VERIFY(c->canChange(0));
        QCOMPARE(c->indexOf("taverna"), -1);
        QVERIFY(!c->canChange(3));
        QVERIFY(!c->step(3, 1));
        c->stepAll(1);
        QTRY_COMPARE(count(SALOTTO_SET), 1);
        idle();
        QCOMPARE(count("AcquaThermoNet/taverna/set_temp"), 0);
    }

    void rate()
    {
        first(0, reading(200));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(200), 5);                              /* same value */
        send(0, reading(202), 4);                              /* change, 9 s */
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(202), 1);                              /* change, 10 s */
        QTRY_COMPARE(count(TOPIC_SALOTTO), 2);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).last()).value("temperature").toDouble(), 20.2);

        send(0, reading(202), 59);                             /* no change, 59 s */
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 2);

        send(0, reading(202), 1);                              /* interval, 60 s */
        QTRY_COMPARE(count(TOPIC_SALOTTO), 3);
    }

    void zonesIndependent()
    {
        first(0, reading(200));
        first(1, reading(190));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        QTRY_COMPARE(count(TOPIC_INGRESSO), 1);
    }

    void outOfRange()
    {
        send(0, reading(501));
        send(0, reading(-201));
        send(0, reading(850));
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 0);
        QCOMPARE(m_mqtt->statsFiltered().at(0), 3u);           /* counted for the diagnostic page */

        first(0, reading(500));                                /* limits included */
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        QCOMPARE(m_mqtt->statsFiltered().at(0), 3u);
        QCOMPARE(m_mqtt->statsFiltered().at(1), 0u);
    }

    void jumpConfirmed()
    {
        first(0, reading(180));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(250), 20);                             /* jump 7 degC */
        send(0, reading(251), 5);                              /* 5 s: too early */
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(251), 5);                              /* 10 s, within 0.5 */
        QTRY_COMPARE(count(TOPIC_SALOTTO), 2);
        QCOMPARE(m_mqtt->statsFiltered().at(0), 2u);           /* the jump and the early one */
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).last()).value("temperature").toDouble(), 25.1);
    }

    void jumpNotConfirmed()
    {
        first(0, reading(180));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(250), 20);                             /* jump */
        send(0, reading(320), 15);                             /* another value: new pending */
        send(0, reading(181), 15);                             /* back: accepted */
        QTRY_COMPARE(count(TOPIC_SALOTTO), 2);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).last()).value("temperature").toDouble(), 18.1);
    }

    void firstReadingConfirmed()
    {
        send(0, reading(200));
        send(0, reading(201), 5);                              /* too early */
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 0);

        send(0, reading(202), 5);                              /* 10 s, within 0.5 */
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).first()).value("temperature").toDouble(), 20.2);
        QCOMPARE(m_mqtt->statsFiltered().at(0), 0u);           /* waiting is not filtering */
    }

    void firstReadingNotConfirmed()
    {
        send(0, reading(450));                                 /* plausible, but wrong */
        send(0, reading(200), 10);                             /* not confirmed: new pending */
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 0);

        send(0, reading(201), 10);
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).first()).value("temperature").toDouble(), 20.1);
    }

    void confirmationAfterSilence()
    {
        first(0, reading(200));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(450), 301);                            /* nothing recent to compare with */
        send(0, reading(201), 10);                             /* new pending */
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 1);

        send(0, reading(201), 10);
        QTRY_COMPARE(count(TOPIC_SALOTTO), 2);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).last()).value("temperature").toDouble(), 20.1);
    }

    void notBufferedWhileDisconnected()
    {
        const quint16 port = m_broker->port();
        m_broker->stop();
        QTRY_VERIFY(m_mqtt->isDisconnected());

        send(0, reading(200));                                 /* dropped */
        QVERIFY(m_broker->listen(port));
        QTRY_COMPARE_WITH_TIMEOUT(m_broker->connects.size(), 2, 5000);  /* backoff 1 s */
        QTRY_COMPARE(count(TOPIC_STATUS), 2);                  /* online again */
        idle(1500);
        QCOMPARE(count(TOPIC_SALOTTO), 0);                     /* nothing sent late */

        first(0, reading(201));
        QTRY_COMPARE(count(TOPIC_SALOTTO), 1);
        QCOMPARE(json(m_broker->on(TOPIC_SALOTTO).first()).value("temperature").toDouble(), 20.1);
    }

    void controllerSubscriptions()
    {
        QTRY_COMPARE(m_broker->subscriptions.size(), 7);
        QVERIFY(m_broker->subscriptions.contains(CTRL_STATUS));
        QVERIFY(m_broker->subscriptions.contains(MODE_STATE));
        QVERIFY(m_broker->subscriptions.contains("AcquaThermoNet/+/state_temp"));
        QVERIFY(m_broker->subscriptions.contains("AcquaThermoNet/+/state_mode"));
        QVERIFY(m_broker->subscriptions.contains("AcquaThermoNet/+/chrono"));
        QVERIFY(m_broker->subscriptions.contains("AcquaThermoNet/+/chrono/profile"));
        QCOMPARE(m_broker->subscriptionQos.value("homeassistant/status", -1), 1);

        /* the retained state arrives at the subscription */
        ZoneControl *c = m_mqtt->zoneControl();
        QTRY_VERIFY(c->canChange(0));
        QVERIFY(c->controllerOnline());
        QCOMPARE(c->zone(0).setPoint, 20.0);
        QVERIFY(c->zone(0).heat);
        QVERIFY(!c->canChange(1));                     /* ingresso: no state_temp */
        QVERIFY(!c->canChange(2));                     /* no zone */
    }

    void setPointRoundTrip()
    {
        ZoneControl *c = m_mqtt->zoneControl();
        QTRY_VERIFY(c->canChange(0));
        QVERIFY(c->step(0, 1));
        QVERIFY(c->step(0, 1));
        QTRY_COMPARE(count(SALOTTO_SET), 1);           /* presses collected: one command */
        const FakeBroker::publish_t p = m_broker->on(SALOTTO_SET).first();
        QCOMPARE(p.payload, QByteArray("21"));         /* plain number */
        QCOMPARE(p.qos, 1);
        QVERIFY(!p.retain);
        QVERIFY(c->zone(0).pending);

        m_broker->deliver(SALOTTO_STATE, "21", true);  /* AcquaThermoNet confirms */
        QTRY_VERIFY(!c->zone(0).pending);
        QCOMPARE(c->zone(0).setPoint, 21.0);
    }

    void houseModeRoundTrip()
    {
        ZoneControl *c = m_mqtt->zoneControl();
        QTRY_VERIFY(c->canChange(0));
        m_broker->deliver(MODE_STATE, R"({"mode":"normal"})", true);
        QTRY_VERIFY(c->canChangeMode());

        QVERIFY(c->requestMode(ZoneControl::Window));
        QTRY_COMPARE(count(MODE_SET), 1);
        const FakeBroker::publish_t p = m_broker->on(MODE_SET).first();
        QCOMPARE(p.payload, QByteArray("window"));
        QCOMPARE(p.qos, 1);
        QVERIFY(!p.retain);

        m_broker->deliver(MODE_STATE, R"({"mode":"window","remaining_s":1800})", true);
        QTRY_COMPARE(c->houseMode(), ZoneControl::Window);
        QVERIFY(!c->modePending());
        QVERIFY(c->remainingS() > 1790);
        QVERIFY(!c->canChange(0));
    }

    void chronoRoundTrip()
    {
        ZoneControl *c = m_mqtt->zoneControl();
        m_broker->deliver(SALOTTO_CHRONO, R"({"chrono":"off"})", true);
        m_broker->deliver(SALOTTO_PROFILE, PROGRAM_OFF, true);
        QTRY_VERIFY(c->canChangeChrono(0));
        QVERIFY(c->chrono(0).hasState);
        QVERIFY(!c->chrono(0).state.on);
        QCOMPARE(c->chrono(0).program.weekday.size(), 1);
        QVERIFY(!c->canChangeChrono(1));               /* ingresso: no program */

        /* on/off: answered by the chrono state */
        QSignalSpy result(c, &ZoneControl::chronoResult);
        QVERIFY(c->setChronoOn(0, true));
        QTRY_COMPARE(count(SALOTTO_CHRONO_SET), 1);
        FakeBroker::publish_t p = m_broker->on(SALOTTO_CHRONO_SET).first();
        QCOMPARE(p.payload, QByteArray("on"));
        QCOMPARE(p.qos, 1);
        QVERIFY(!p.retain);
        m_broker->deliver(SALOTTO_CHRONO, R"({"chrono":"on","manual":false,"next_change":"06:30","next_setpoint":20.5,"paused":false,"profile":"weekday"})", true);
        QTRY_COMPARE(result.count(), 1);
        QCOMPARE(result.at(0).at(1).value<ZoneControl::ChronoResult>(), ZoneControl::ChronoSaved);
        QCOMPARE(c->chrono(0).state.nextChange, QString("06:30"));

        /* a program: answered by the program */
        ChronoConfig prog = c->chrono(0).program;
        prog.enabled = true;
        prog.weekday.append({ QTime(22, 30), 17 });
        QVERIFY(c->saveProgram(0, prog));
        QTRY_COMPARE(count(SALOTTO_PROFILE_SET), 1);
        p = m_broker->on(SALOTTO_PROFILE_SET).first();
        QCOMPARE(p.payload, Chrono::programCommand(prog));
        QCOMPARE(p.qos, 1);
        QVERIFY(!p.retain);
        m_broker->deliver(SALOTTO_PROFILE, R"({"edited":true,"enabled":true,"holiday":[],"holiday_days":[6,7],)"
                                           R"("weekday":[{"at":"06:30","temp":20.5},{"at":"22:30","temp":17}]})", true);
        QTRY_COMPARE(result.count(), 2);
        QCOMPARE(result.at(1).at(1).value<ZoneControl::ChronoResult>(), ZoneControl::ChronoSaved);
        QVERIFY(c->chrono(0).program.edited);

        /* reset */
        QVERIFY(c->resetProgram(0));
        QTRY_COMPARE(count(SALOTTO_PROFILE_SET), 2);
        QCOMPARE(m_broker->on(SALOTTO_PROFILE_SET).last().payload, QByteArray(R"({"reset":true})"));
        m_broker->deliver(SALOTTO_PROFILE, PROGRAM_OFF, true);
        QTRY_COMPARE(result.count(), 3);
        QCOMPARE(result.at(2).at(1).value<ZoneControl::ChronoResult>(), ZoneControl::ChronoSaved);
        QVERIFY(!c->chrono(0).program.edited);
    }

    void noCommandWhenControllerOffline()
    {
        ZoneControl *c = m_mqtt->zoneControl();
        QTRY_VERIFY(c->canChange(0));
        m_broker->deliver(CTRL_STATUS, "offline", true);
        QTRY_VERIFY(!c->controllerOnline());
        QVERIFY(!c->step(0, 1));
        idle(1500);
        QCOMPARE(count(SALOTTO_SET), 0);
    }

    void shutdown()
    {
        m_mqtt->shutdown();
        QTRY_VERIFY(m_mqtt->isDisconnected());
        QTRY_COMPARE(m_broker->disconnects, 1);                /* clean: no Will */
        const FakeBroker::publish_t last = m_broker->publishes.last();
        QCOMPARE(last.topic, QString(TOPIC_STATUS));
        QCOMPARE(last.payload, QByteArray("offline"));
        QVERIFY(last.retain);

        idle(2500);                                            /* no reconnect */
        QCOMPARE(m_broker->connects.size(), 1);
        send(0, reading(200));
        idle();
        QCOMPARE(count(TOPIC_SALOTTO), 0);
    }
};

int runMqttTests(int argc, char *argv[])
{
    TstMqtt t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mqtt.moc"
