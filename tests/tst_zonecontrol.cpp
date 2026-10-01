#include <QSignalSpy>
#include <QTest>
#include "monoclock.h"
#include "zonecontrol.h"

static const int DELAY_MS = 50;         /* presses collected */
static const int CONFIRM_MS = 300;      /* confirmation timeout */

class TstZoneControl : public QObject
{
    Q_OBJECT

private:
    /* Two zones and one without zone, controller online, salotto at 20 */
    ZoneControl *online()
    {
        ZoneControl *c = new ZoneControl({ "salotto", "camera", "" }, DELAY_MS, CONFIRM_MS, this);
        c->setConnected(true);
        c->onStatus("online");
        c->onStateTemp("salotto", "20");
        return c;
    }

private slots:
    void normalize_data()
    {
        QTest::addColumn<double>("in");
        QTest::addColumn<double>("out");
        QTest::newRow("round up")   << 21.3 << 21.5;
        QTest::newRow("round down") << 21.2 << 21.0;
        QTest::newRow("max")        << 40.0 << 25.0;
        QTest::newRow("min")        << 2.0  << 5.0;
        QTest::newRow("negative")   << -3.0 << 5.0;
    }

    void normalize()
    {
        QFETCH(double, in);
        QFETCH(double, out);
        QCOMPARE(ZoneControl::normalize(in), out);
    }

    void notReachableAtStart()
    {
        ZoneControl c({ "salotto" }, DELAY_MS, CONFIRM_MS);
        QVERIFY(!c.controllerOnline());
        QVERIFY(!c.canChange(0));
        QVERIFY(!c.step(0, 1));

        c.setConnected(true);
        c.onStatus("online");
        QVERIFY(c.controllerOnline());
        QVERIFY(!c.canChange(0));                       /* no state_temp yet */
        c.onStateTemp("salotto", "20");
        QVERIFY(c.canChange(0));
        QCOMPARE(c.zone(0).setPoint, 20.0);
    }

    void pressesCollected()
    {
        ZoneControl *c = online();
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        QVERIFY(c->step(0, 1));
        QVERIFY(c->step(0, 1));
        QVERIFY(c->step(0, 1));
        QVERIFY(c->zone(0).pending);
        QCOMPARE(c->zone(0).pendingValue, 21.5);        /* shown at once */
        QCOMPARE(c->zone(0).setPoint, 20.0);            /* not confirmed */
        QCOMPARE(sent.count(), 0);

        QTRY_COMPARE(sent.count(), 1);                  /* one command */
        QCOMPARE(sent.at(0).at(0).toString(), QString("salotto"));
        QCOMPARE(sent.at(0).at(1).toDouble(), 21.5);
        QTest::qWait(DELAY_MS * 3);
        QCOMPARE(sent.count(), 1);

        c->onStateTemp("salotto", "21.5");              /* confirmation */
        QVERIFY(!c->zone(0).pending);
        QCOMPARE(c->zone(0).setPoint, 21.5);
    }

    void confirmationMayDiffer()
    {
        /* clamped, or changed meanwhile from the controller panel */
        ZoneControl *c = online();
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        c->step(0, 2);
        QTRY_COMPARE(sent.count(), 1);
        c->onStateTemp("salotto", "19");
        QVERIFY(!c->zone(0).pending);
        QCOMPARE(c->zone(0).setPoint, 19.0);
    }

    void stateBeforeTheCommandKeepsPending()
    {
        ZoneControl *c = online();
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        c->step(0, 1);
        c->onStateTemp("salotto", "18");                /* from elsewhere, before ours */
        QVERIFY(c->zone(0).pending);
        QCOMPARE(c->zone(0).setPoint, 18.0);
        QCOMPARE(c->zone(0).pendingValue, 20.5);
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(1).toDouble(), 20.5);
    }

    void clamped()
    {
        ZoneControl *c = online();
        c->onStateTemp("salotto", "24.5");
        c->step(0, 3);
        QCOMPARE(c->zone(0).pendingValue, 25.0);
        c->onStateTemp("camera", "5.5");
        c->step(1, -4);
        QCOMPARE(c->zone(1).pendingValue, 5.0);
    }

    void notConfirmed()
    {
        ZoneControl *c = online();
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        c->step(0, 2);
        QTRY_COMPARE(sent.count(), 1);
        QVERIFY(c->zone(0).pending);
        QTRY_VERIFY_WITH_TIMEOUT(!c->zone(0).pending, CONFIRM_MS * 4);   /* back to 20 */
        QCOMPARE(c->zone(0).setPoint, 20.0);
    }

    void controllerOffline()
    {
        ZoneControl *c = online();
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        QSignalSpy onlineSpy(c, &ZoneControl::controllerOnlineChanged);
        c->step(0, 1);
        c->onStatus("offline");
        QCOMPARE(onlineSpy.count(), 1);
        QCOMPARE(onlineSpy.at(0).at(0).toBool(), false);
        QVERIFY(!c->zone(0).pending);                   /* change dropped */
        QVERIFY(!c->step(0, 1));
        QTest::qWait(DELAY_MS * 3);
        QCOMPARE(sent.count(), 0);
        QCOMPARE(c->zone(0).setPoint, 20.0);            /* last known, still shown */

        c->onStatus("online");
        QVERIFY(c->step(0, 1));
    }

    void brokerDisconnected()
    {
        ZoneControl *c = online();
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        c->step(0, 1);
        c->setConnected(false);
        QVERIFY(!c->controllerOnline());
        QVERIFY(!c->zone(0).pending);
        QVERIFY(!c->step(0, 1));
        QTest::qWait(DELAY_MS * 3);
        QCOMPARE(sent.count(), 0);
    }

    void ignored()
    {
        ZoneControl *c = online();
        QSignalSpy changed(c, &ZoneControl::zoneChanged);
        c->onStateTemp("cucina", "21");                 /* not one of ours */
        c->onStateTemp("salotto", "abc");               /* not a number */
        c->onStateMode("cucina", "heat");
        QCOMPARE(changed.count(), 0);
        QCOMPARE(c->zone(0).setPoint, 20.0);
        QVERIFY(!c->canChange(2));                      /* sensor without zone */
        QVERIFY(!c->step(2, 1));
        QCOMPARE(c->indexOf(""), -1);
    }

    void stepAll()
    {
        ZoneControl *c = online();                      /* salotto 20 */
        c->onStateTemp("camera", "18");
        QSignalSpy sent(c, &ZoneControl::sendSetPoint);
        double low, high;
        QVERIFY(c->range(low, high));
        QCOMPARE(low, 18.0);
        QCOMPARE(high, 20.0);

        QVERIFY(c->stepAll(-1));                        /* aligned to the lowest */
        QCOMPARE(c->zone(0).pendingValue, 18.0);
        QCOMPARE(c->zone(1).pendingValue, 18.0);
        QVERIFY(c->stepAll(-1));                        /* aligned: -0.5 */
        QCOMPARE(c->zone(0).pendingValue, 17.5);
        QCOMPARE(c->zone(1).pendingValue, 17.5);
        QTRY_COMPARE(sent.count(), 2);                  /* one command per zone */
        QCOMPARE(sent.at(0).at(1).toDouble(), 17.5);
        QCOMPARE(sent.at(1).at(1).toDouble(), 17.5);
        QVERIFY(!c->zone(2).pending);                   /* no zone: untouched */

        c->onStateTemp("salotto", "22");                /* confirmations, then + */
        c->onStateTemp("camera", "17.5");
        QVERIFY(c->stepAll(1));                         /* aligned to the highest */
        QCOMPARE(c->zone(1).pendingValue, 22.0);
        QVERIFY(c->stepAll(1));
        QVERIFY(c->stepAll(1));
        QCOMPARE(c->zone(0).pendingValue, 23.0);
        QCOMPARE(c->zone(1).pendingValue, 23.0);
        for(int i = 0; i < 10; i++)
            c->stepAll(1);
        QCOMPARE(c->zone(0).pendingValue, 25.0);        /* clamped */
    }

    void stepAllNotPossible()
    {
        ZoneControl *c = online();
        c->onStateTemp("camera", "18");
        c->onModeState(R"({"mode":"away"})");
        double low, high;
        QVERIFY(!c->range(low, high));                  /* during a house mode */
        QVERIFY(!c->stepAll(-1));
        c->onModeState(R"({"mode":"normal"})");
        c->onStatus("offline");
        QVERIFY(!c->stepAll(1));

        ZoneControl *one = online();                    /* only salotto has a setpoint */
        QVERIFY(one->range(low, high));
        QCOMPARE(low, 20.0);
        QVERIFY(one->stepAll(-1));                      /* a single zone: -0.5 */
        QCOMPARE(one->zone(0).pendingValue, 19.5);
        QVERIFY(!one->zone(1).pending);
    }

    void houseMode()
    {
        ZoneControl *c = online();
        QVERIFY(!c->hasHouseMode());
        QVERIFY(!c->canChangeMode());                   /* no mode/state yet */
        QVERIFY(!c->requestMode(ZoneControl::Away));

        QSignalSpy changed(c, &ZoneControl::houseModeChanged);
        QSignalSpy sent(c, &ZoneControl::sendMode);
        c->onModeState(R"({"mode":"normal"})");
        QVERIFY(c->hasHouseMode());
        QCOMPARE(c->houseMode(), ZoneControl::Normal);
        QCOMPARE(changed.count(), 1);
        QVERIFY(c->canChange(0));

        /* a setpoint change in progress is dropped by the mode request */
        QVERIFY(c->step(0, 1));
        QVERIFY(c->requestMode(ZoneControl::Away));
        QVERIFY(!c->zone(0).pending);
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(0).toString(), QString("away"));
        QVERIFY(c->modePending());
        QCOMPARE(c->pendingMode(), ZoneControl::Away);
        QVERIFY(!c->canChange(0));                      /* while pending */
        QVERIFY(!c->step(0, 1));
        QTest::qWait(DELAY_MS * 3);
        QVERIFY(!c->zone(0).pending);                   /* no set_temp either */

        c->onModeState(R"({"mode":"away"})");
        QVERIFY(!c->modePending());
        QCOMPARE(c->houseMode(), ZoneControl::Away);
        QVERIFY(!c->canChange(0));                      /* -/+ disabled during a mode */
        QVERIFY(c->canChangeMode());
        QCOMPARE(c->remainingS(), 0);

        c->onModeState(R"({"mode":"normal"})");
        QVERIFY(c->canChange(0));
    }

    void windowCountdown()
    {
        ZoneControl *c = online();
        c->onModeState(R"({"mode":"window","remaining_s":120})");
        QCOMPARE(c->houseMode(), ZoneControl::Window);
        QCOMPARE(c->remainingS(), 120);
        MonoClock::advanceForTest(50 * 1000);
        QCOMPARE(c->remainingS(), 70);
        MonoClock::advanceForTest(100 * 1000);
        QCOMPARE(c->remainingS(), 0);              /* never negative */
        c->onModeState(R"({"mode":"window","remaining_s":60})");   /* a new one replaces it */
        QCOMPARE(c->remainingS(), 60);
        c->onModeState(R"({"mode":"boost","remaining_s":1800})");
        QCOMPARE(c->houseMode(), ZoneControl::Boost);
        QCOMPARE(c->remainingS(), 1800);
        QVERIFY(!c->canChange(0));                      /* -/+ disabled */
        c->onModeState(R"({"mode":"away"})");
        QCOMPARE(c->remainingS(), 0);
    }

    void modeNotConfirmed()
    {
        ZoneControl *c = online();
        c->onModeState(R"({"mode":"normal"})");
        QVERIFY(c->requestMode(ZoneControl::Window));
        QVERIFY(c->modePending());
        QTRY_VERIFY_WITH_TIMEOUT(!c->modePending(), CONFIRM_MS * 4);
        QCOMPARE(c->houseMode(), ZoneControl::Normal);
    }

    void modeOffline()
    {
        ZoneControl *c = online();
        c->onModeState(R"({"mode":"away"})");
        QVERIFY(c->requestMode(ZoneControl::Normal));
        c->onStatus("offline");
        QVERIFY(!c->modePending());                     /* dropped */
        QVERIFY(!c->canChangeMode());
        QVERIFY(!c->requestMode(ZoneControl::Normal));
        QCOMPARE(c->houseMode(), ZoneControl::Away);    /* last known */
    }

    void modeStateInvalid()
    {
        ZoneControl *c = online();
        c->onModeState("away");                         /* not JSON */
        c->onModeState(R"({"mode":"holiday"})");
        QVERIFY(!c->hasHouseMode());
        QCOMPARE(ZoneControl::modeName(ZoneControl::Window), QString("window"));
    }

    void heat()
    {
        ZoneControl *c = online();
        QVERIFY(!c->zone(1).hasHeat);
        c->onStateMode("camera", "heat");
        QVERIFY(c->zone(1).hasHeat);
        QVERIFY(c->zone(1).heat);
        c->onStateMode("camera", "off");
        QVERIFY(!c->zone(1).heat);
    }
};

int runZoneControlTests(int argc, char *argv[])
{
    TstZoneControl t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_zonecontrol.moc"
