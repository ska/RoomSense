#include <QTest>
#include "zoneview.h"

using namespace ZoneView;

/* A heard sensor, 30 s ago, 21.3 degC, controller online at 21.5 */
static input_t heard()
{
    input_t in;
    in.heard = true;
    in.ageS = 30;
    in.temp = 213;
    in.hum = 48;
    in.batt = 85;
    in.control.hasSetPoint = true;
    in.control.setPoint = 21.5;
    in.control.hasHeat = true;
    in.control.heat = true;
    in.controllerOnline = true;
    return in;
}

class TstZoneView : public QObject
{
    Q_OBJECT

private slots:
    void normal()
    {
        const view_t v = make(heard());
        QCOMPARE(v.level, Ok);
        QCOMPARE(v.tempText, QString("21.3°"));
        QVERIFY(!v.tempMuted);
        QCOMPARE(v.statusText, QString("updated 30 s ago"));
        QCOMPARE(v.humText, QString("48 %"));
        QCOMPARE(v.battText, QString("85 %"));
        QVERIFY(!v.battLow);
        QCOMPARE(v.heat, HeatOn);
        QCOMPARE(v.heatText, QString("Heating"));
        QCOMPARE(v.setPoint, Confirmed);
        QCOMPARE(v.setPointText, QString("21.5"));
        QCOMPARE(v.setPointLabel, QString("setpoint"));
    }

    void levels_data()
    {
        QTest::addColumn<qint64>("age");
        QTest::addColumn<int>("level");
        QTest::addColumn<QString>("status");
        QTest::newRow("2 min")      << qint64(120)  << int(Ok)    << "updated 2 min ago";
        QTest::newRow("5 min")      << qint64(300)  << int(Ok)    << "updated 5 min ago";
        QTest::newRow("just stale") << qint64(301)  << int(Stale) << "last reading 5 min ago";
        QTest::newRow("15 min")     << qint64(900)  << int(Stale) << "last reading 15 min ago";
        QTest::newRow("just lost")  << qint64(901)  << int(Lost)  << "NO SENSOR for 15 min";
        QTest::newRow("3 h")        << qint64(10800)<< int(Lost)  << "NO SENSOR for 3 h";
    }

    void levels()
    {
        QFETCH(qint64, age);
        QFETCH(int, level);
        QFETCH(QString, status);
        input_t in = heard();
        in.ageS = age;
        const view_t v = make(in);
        QCOMPARE(int(v.level), level);
        QCOMPARE(v.statusText, status);
    }

    void lost()
    {
        input_t in = heard();
        in.ageS = 1000;
        const view_t v = make(in);
        QCOMPARE(v.tempText, QString("--.-°"));          /* never an old value */
        QVERIFY(v.tempMuted);
        QCOMPARE(v.humText, QString("--"));
        QCOMPARE(v.battText, QString("--"));
        QCOMPARE(v.heat, HeatNoSensor);                  /* AcquaThermoNet closed the zone */
        QCOMPARE(v.heatText, QString("Off, no sensor"));
        QCOMPARE(v.setPoint, Confirmed);                 /* still changeable */
    }

    void neverHeard()
    {
        input_t in = heard();
        in.heard = false;
        const view_t v = make(in);
        QCOMPARE(v.level, Never);
        QCOMPARE(v.statusText, QString("no reading yet"));
        QCOMPARE(v.tempText, QString("--.-°"));
    }

    void unknownValues()
    {
        input_t in = heard();
        in.hum = 0;
        in.batt = 0;
        view_t v = make(in);
        QCOMPARE(v.humText, QString("--"));
        QCOMPARE(v.battText, QString("--"));
        QVERIFY(!v.battLow);

        in.batt = 15;
        v = make(in);
        QCOMPARE(v.battText, QString("15 % low"));
        QVERIFY(v.battLow);
        in.batt = 20;
        QVERIFY(!make(in).battLow);                      /* AcquaThermoNet alarm below 20 */
    }

    void negative()
    {
        input_t in = heard();
        in.temp = -53;
        QCOMPARE(make(in).tempText, QString("-5.3°"));
    }

    void heat()
    {
        input_t in = heard();
        in.control.heat = false;
        QCOMPARE(make(in).heatText, QString("Idle"));
        in.control.hasHeat = false;
        QCOMPARE(make(in).heat, HeatUnknown);
        QVERIFY(make(in).heatText.isEmpty());
    }

    void setPoints()
    {
        input_t in = heard();
        in.control.pending = true;
        in.control.pendingValue = 22.0;
        view_t v = make(in);
        QCOMPARE(v.setPoint, Pending);
        QCOMPARE(v.setPointText, QString("22.0"));
        QCOMPARE(v.setPointLabel, QString("sending…"));

        in.controllerOnline = false;
        v = make(in);
        QCOMPARE(v.setPoint, LastKnown);
        QCOMPARE(v.setPointText, QString("21.5"));       /* the confirmed one */
        QCOMPARE(v.setPointLabel, QString("last known"));

        in.control.hasSetPoint = false;
        v = make(in);
        QCOMPARE(v.setPoint, NoSetPoint);
        QCOMPARE(v.setPointText, QString("--.-"));

        in.hasZone = false;
        v = make(in);
        QCOMPARE(v.setPoint, NoZone);
        QCOMPARE(v.setPointLabel, QString("no zone"));
        QCOMPARE(v.heat, HeatUnknown);
    }

    void houseMode()
    {
        input_t in = heard();
        in.control.setPoint = 8;                        /* state_temp: the mode value */
        in.houseMode = ZoneControl::Window;
        view_t v = make(in);
        QCOMPARE(v.setPoint, InMode);
        QCOMPARE(v.setPointText, QString("8.0"));
        QCOMPARE(v.setPointLabel, QString("windows open"));
        in.houseMode = ZoneControl::Away;
        QCOMPARE(make(in).setPointLabel, QString("away"));
        in.houseMode = ZoneControl::Boost;
        QCOMPARE(make(in).setPointLabel, QString("boost"));
        QCOMPARE(make(in).setPoint, InBoost);
        in.controllerOnline = false;
        QCOMPARE(make(in).setPoint, LastKnown);
    }

    void modeButtonTexts()
    {
        mode_input_t in;
        mode_view_t v = modeButton(in);
        QCOMPARE(v.text, QString("Unknown"));
        QCOMPARE(v.level, QString("unknown"));

        in.hasMode = true;
        v = modeButton(in);
        QCOMPARE(v.text, QString("Normal"));
        QCOMPARE(v.detail, QString());
        QCOMPARE(v.icon, QString("home"));

        in.mode = ZoneControl::Window;
        in.remainingS = 23 * 60 - 10;
        v = modeButton(in);
        QCOMPARE(v.text, QString("Windows open"));
        QCOMPARE(v.detail, QString("23 min left"));
        QCOMPARE(v.icon, QString("window"));
        QCOMPARE(v.level, QString("window"));
        in.remainingS = 45;
        QCOMPARE(modeButton(in).detail, QString("45 s left"));

        in.mode = ZoneControl::Away;
        QCOMPARE(modeButton(in).text, QString("Away"));
        QCOMPARE(modeButton(in).detail, QString());
        QCOMPARE(modeButton(in).icon, QString("plane"));

        in.mode = ZoneControl::Boost;
        in.remainingS = 1800;
        v = modeButton(in);
        QCOMPARE(v.text, QString("Boost"));
        QCOMPARE(v.detail, QString("30 min left"));
        QCOMPARE(v.icon, QString("flame"));
        QCOMPARE(v.level, QString("boost"));

        in.pending = true;
        in.pendingMode = ZoneControl::Normal;
        v = modeButton(in);
        QCOMPARE(v.text, QString::fromUtf8("Sending…"));
        QCOMPARE(v.level, QString("pending"));
    }

    void chronoLine_data()
    {
        QTest::addColumn<QByteArray>("state");
        QTest::addColumn<bool>("saving");
        QTest::addColumn<bool>("online");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("level");
        const QByteArray next = R"({"chrono":"on","manual":false,"next_change":"22:30","next_setpoint":17,"paused":false})";
        QTest::newRow("next change")    << next << false << true << QString::fromUtf8("22:30 → 17.0°") << "next";
        QTest::newRow("manual")         << QByteArray(R"({"chrono":"on","manual":true,"next_change":"22:30","next_setpoint":17,"paused":false})")
                                        << false << true << QString::fromUtf8("manual → 22:30") << "manual";
        QTest::newRow("paused (away)")  << QByteArray(R"({"chrono":"on","manual":false,"next_change":"22:30","next_setpoint":17,"paused":true})")
                                        << false << true << "chrono paused" << "paused";
        QTest::newRow("no clock")       << QByteArray(R"({"chrono":"on","manual":false,"paused":false})")
                                        << false << true << "chrono on" << "next";
        QTest::newRow("off")            << QByteArray(R"({"chrono":"off"})") << false << true << "chrono off" << "off";
        QTest::newRow("saving")         << next << true << true << QString::fromUtf8("saving…") << "saving";
        QTest::newRow("last known")     << next << false << false << QString::fromUtf8("22:30 → 17.0°") << "lastknown";
    }

    void chronoLine()
    {
        QFETCH(QByteArray, state);
        QFETCH(bool, saving);
        QFETCH(bool, online);
        QFETCH(QString, text);
        QFETCH(QString, level);
        chrono_input_t in;
        QVERIFY(Chrono::parseState(state, in.chrono.state));
        in.chrono.hasState = true;
        in.chrono.saving = saving;
        in.controllerOnline = online;
        chrono_view_t v = ZoneView::chronoLine(in);
        QVERIFY(v.shown);
        QVERIFY(!v.tappable);                           /* no program */
        in.chrono.hasProgram = true;
        QVERIFY(ZoneView::chronoLine(in).tappable);
        QCOMPARE(v.text, text);
        QCOMPARE(v.level, level);
    }

    void chronoLineHidden()
    {
        chrono_input_t in;
        in.controllerOnline = true;
        QVERIFY(!ZoneView::chronoLine(in).shown);       /* no state from the controller */
        in.chrono.hasState = true;
        in.hasZone = false;
        QVERIFY(!ZoneView::chronoLine(in).shown);
    }

    void age()
    {
        QCOMPARE(ZoneView::age(0), QString("0 s"));
        QCOMPARE(ZoneView::age(59), QString("59 s"));
        QCOMPARE(ZoneView::age(60), QString("1 min"));
        QCOMPARE(ZoneView::age(3599), QString("59 min"));
        QCOMPARE(ZoneView::age(7200), QString("2 h"));
    }
};

int runZoneViewTests(int argc, char *argv[])
{
    TstZoneView t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_zoneview.moc"
