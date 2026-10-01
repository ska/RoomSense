#include <QTest>
#include "fakescenario.h"

static int temp(const FakeScenario &f, int sensor, qint64 t)
{
    sensor_env_t e;
    return f.reading(sensor, t, e) ? e.temp : INT_MIN;
}

static bool heard(const FakeScenario &f, int sensor, qint64 t)
{
    sensor_env_t e;
    return f.reading(sensor, t, e);
}

class TstFakeScenario : public QObject
{
    Q_OBJECT

private slots:
    void normal()
    {
        const FakeScenario f(3);
        QVERIFY(f.isValid());
        for(qint64 t = 0; t < 3600; t += FakeScenario::PERIOD_S)
        {
            for(int s = 0; s < 3; s++)
            {
                sensor_env_t e;
                QVERIFY(f.reading(s, t, e));
                QVERIFY(e.temp >= 165 && e.temp <= 225);       /* 16.5..22.5 degC */
                QVERIFY(e.hum >= 40 && e.hum <= 60);
                QVERIFY(e.batt >= 1 && e.batt <= 100);
                QVERIFY(e.battmv > 0);
                if(t > 0)                                       /* slow drift */
                    QVERIFY(qAbs(e.temp - temp(f, s, t - FakeScenario::PERIOD_S)) <= 1);
            }
        }
        QCOMPARE(temp(f, 1, 100), temp(FakeScenario(3), 1, 100)); /* deterministic */
        QVERIFY(!heard(f, 3, 0));                               /* no such sensor */
    }

    void lostAndBack()
    {
        const FakeScenario f(2, "lost=1@20; back=1@60");
        QVERIFY(f.isValid());
        QVERIFY(heard(f, 1, 18));
        QVERIFY(!heard(f, 1, 20));
        QVERIFY(!heard(f, 1, 58));
        QVERIFY(heard(f, 1, 60));
        QVERIFY(heard(f, 0, 30));                               /* the others go on */
    }

    void spikeOutStep()
    {
        const FakeScenario plain(1);
        const FakeScenario f(1, "spike=0@10:+80;out=0@20:850;step=0@30:-40");
        QVERIFY(f.isValid());
        QCOMPARE(temp(f, 0, 8),  temp(plain, 0, 8));
        QCOMPARE(temp(f, 0, 10), temp(plain, 0, 10) + 80);      /* one reading only */
        QCOMPARE(temp(f, 0, 12), temp(plain, 0, 12));
        QCOMPARE(temp(f, 0, 20), 850);
        QCOMPARE(temp(f, 0, 22), temp(plain, 0, 22));
        QCOMPARE(temp(f, 0, 30), temp(plain, 0, 30) - 40);      /* from then on */
        QCOMPARE(temp(f, 0, 300), temp(plain, 0, 300) - 40);
    }

    void coldAndNoBattery()
    {
        const FakeScenario f(2, "cold=0;nobatt=1");
        sensor_env_t e;
        QVERIFY(f.reading(0, 100, e));
        QVERIFY(e.temp < 0);                                    /* around -5.0 degC */
        QVERIFY(e.batt > 0);
        QVERIFY(f.reading(1, 100, e));
        QVERIFY(e.temp > 0);
        QCOMPARE(int(e.batt), 0);
        QCOMPARE(int(e.battmv), 0);
    }

    void presses()
    {
        const FakeScenario f(2, "press=1@8:+2;press=0@20:-1");
        QVERIFY(f.isValid());
        QCOMPARE(f.presses(0, 8).size(), 0);
        const auto p = f.presses(8, 9);
        QCOMPARE(p.size(), 1);
        QCOMPARE(p.at(0).first, 1);
        QCOMPARE(p.at(0).second, 2);
        QCOMPARE(f.presses(9, 20).size(), 0);
        QCOMPARE(f.presses(20, 21).at(0).second, -1);
        QVERIFY(!FakeScenario(2, "press=1@8").isValid());      /* steps missing */

        const FakeScenario m(2, "mode=window@5;mode=normal@30");
        QVERIFY(m.isValid());
        QCOMPARE(m.modes(0, 5).size(), 0);
        QCOMPARE(m.modes(5, 6), QStringList({ "window" }));
        QCOMPARE(m.modes(6, 40), QStringList({ "normal" }));
        QVERIFY(FakeScenario(2, "mode=boost@5").isValid());
        QVERIFY(!FakeScenario(2, "mode=holiday@5").isValid());
        QVERIFY(!FakeScenario(2, "mode=away").isValid());       /* time missing */
    }

    void invalid_data()
    {
        QTest::addColumn<QString>("spec");
        QTest::newRow("unknown")        << "melt=0@10";
        QTest::newRow("no sensor")      << "lost=@10";
        QTest::newRow("sensor too big") << "lost=5@10";
        QTest::newRow("no time")        << "lost=0";
        QTest::newRow("value missing")  << "spike=0@10";
        QTest::newRow("value extra")    << "lost=0@10:5";
        QTest::newRow("cold with time") << "cold=0@10";
        QTest::newRow("one bad of two") << "lost=0@10;foo";
    }

    void invalid()
    {
        QFETCH(QString, spec);
        const FakeScenario f(2, spec);
        QVERIFY(!f.isValid());
        QVERIFY(!f.error().isEmpty());
        QVERIFY(heard(f, 0, 100));                              /* normal drift only */
        QCOMPARE(temp(f, 0, 100), temp(FakeScenario(2), 0, 100));
    }
};

int runFakeScenarioTests(int argc, char *argv[])
{
    TstFakeScenario t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_fakescenario.moc"
