#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include "chrono.h"
#include <algorithm>

/* 2026-09-25 is a Friday, 26 Saturday, 27 Sunday, 28 Monday */
static QDateTime at(int day, int h, int m)
{
    return QDateTime(QDate(2026, 9, day), QTime(h, m));
}

static QVector<ChronoSlot> makeSlots(std::initializer_list<ChronoSlot> list)
{
    return QVector<ChronoSlot>(list);
}

/* the program of the interface example (salotto) */
static const char *EXAMPLE =
    R"({"edited":true,"enabled":true,"holiday":[{"at":"08:00","temp":20.5},{"at":"23:00","temp":17}],)"
    R"("holiday_days":[6,7],"weekday":[{"at":"06:30","temp":20.5},{"at":"08:00","temp":18},)"
    R"({"at":"17:00","temp":20.5},{"at":"22:30","temp":17}]})";

static ChronoConfig example()
{
    ChronoConfig c;
    Chrono::parseProgram(EXAMPLE, c);
    return c;
}

class TstChrono : public QObject
{
    Q_OBJECT

private slots:
    void parseProgram()
    {
        ChronoConfig c;
        QVERIFY(Chrono::parseProgram(EXAMPLE, c));
        QVERIFY(c.enabled);
        QVERIFY(c.edited);
        QCOMPARE(c.holidayDays, quint8((1 << 6) | (1 << 7)));
        QCOMPARE(Chrono::toString(c.weekday), QString("06:30=20.5, 08:00=18.0, 17:00=20.5, 22:30=17.0"));
        QCOMPARE(Chrono::toString(c.holiday), QString("08:00=20.5, 23:00=17.0"));

        /* off, empty profiles, no holiday days */
        QVERIFY(Chrono::parseProgram(R"({"edited":false,"enabled":false,"holiday":[],"holiday_days":[],"weekday":[]})", c));
        QVERIFY(!c.enabled);
        QVERIFY(!c.edited);
        QVERIFY(c.weekday.isEmpty());
        QCOMPARE(c.holidayDays, quint8(0));
    }

    void parseProgramInvalid_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::newRow("not json")       << QByteArray("on");
        QTest::newRow("no enabled")     << QByteArray(R"({"holiday":[],"holiday_days":[],"weekday":[]})");
        QTest::newRow("no days")        << QByteArray(R"({"enabled":false,"holiday":[],"weekday":[]})");
        QTest::newRow("day 8")          << QByteArray(R"({"enabled":false,"holiday":[],"holiday_days":[8],"weekday":[]})");
        QTest::newRow("no weekday")     << QByteArray(R"({"enabled":false,"holiday":[],"holiday_days":[]})");
        QTest::newRow("bad time")       << QByteArray(R"({"enabled":true,"holiday":[],"holiday_days":[],"weekday":[{"at":"6:30","temp":20}]})");
        QTest::newRow("temp string")    << QByteArray(R"({"enabled":true,"holiday":[],"holiday_days":[],"weekday":[{"at":"06:30","temp":"20"}]})");
    }

    void parseProgramInvalid()
    {
        QFETCH(QByteArray, payload);
        ChronoConfig c = example();
        QVERIFY(!Chrono::parseProgram(payload, c));
        QCOMPARE(c.weekday.size(), 4);          /* untouched */
    }

    void parseState()
    {
        ChronoState s;
        QVERIFY(Chrono::parseState(R"({"chrono":"on","manual":true,"next_change":"22:30",)"
                                   R"("next_change_at":"2026-10-03T22:30:00+02:00","next_setpoint":17,)"
                                   R"("paused":false,"profile":"weekday"})", s));
        QVERIFY(s.on);
        QVERIFY(s.manual);
        QVERIFY(!s.paused);
        QCOMPARE(s.profile, QString("weekday"));
        QVERIFY(s.hasNext);
        QCOMPARE(s.nextChange, QString("22:30"));
        QCOMPARE(s.nextSetpoint, 17.0);

        /* clock of the controller not set: no next change */
        QVERIFY(Chrono::parseState(R"({"chrono":"on","manual":false,"paused":true})", s));
        QVERIFY(s.on);
        QVERIFY(s.paused);
        QVERIFY(!s.hasNext);

        QVERIFY(Chrono::parseState(R"({"chrono":"off"})", s));
        QVERIFY(!s.on);
        QVERIFY(!s.hasNext);

        s.on = true;
        QVERIFY(!Chrono::parseState(R"({"chrono":"maybe"})", s));
        QVERIFY(!Chrono::parseState("off", s));
        QVERIFY(s.on);                          /* untouched */
    }

    void programCommand()
    {
        ChronoConfig c = example();
        const QByteArray cmd = Chrono::programCommand(c);
        QVERIFY(!cmd.contains('\n'));
        const QJsonObject o = QJsonDocument::fromJson(cmd).object();
        QCOMPARE(o.keys(), QStringList({ "enabled", "holiday", "weekday" }));   /* no holiday_days */
        QCOMPARE(o["enabled"].toBool(), true);
        QCOMPARE(o["weekday"].toArray().size(), 4);
        QCOMPARE(o["weekday"].toArray().first().toObject(), QJsonObject({ { "at", "06:30" }, { "temp", 20.5 } }));
        QVERIFY(cmd.contains(R"({"at":"22:30","temp":17})"));

        /* what the controller publishes back is the same program */
        ChronoConfig back;
        QJsonObject full = o;
        full.insert("edited", true);
        full.insert("holiday_days", QJsonArray({ 6, 7 }));
        QVERIFY(Chrono::parseProgram(QJsonDocument(full).toJson(), back));
        QVERIFY(Chrono::sameProgram(c, back));
    }

    void sameProgram()
    {
        const ChronoConfig a = example();
        ChronoConfig b = a;
        b.edited = false;
        b.holidayDays = 0;
        QVERIFY(Chrono::sameProgram(a, b));     /* not part of the command */
        b.weekday[1].temp = 18.2;               /* rounded by the controller */
        std::reverse(b.weekday.begin(), b.weekday.end());
        QVERIFY(Chrono::sameProgram(a, b));
        b.weekday[0].temp = 16;
        QVERIFY(!Chrono::sameProgram(a, b));
        b = a;
        b.enabled = false;
        QVERIFY(!Chrono::sameProgram(a, b));
        b = a;
        b.holiday.clear();
        QVERIFY(!Chrono::sameProgram(a, b));
    }

    void current_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<double>("temp");
        QTest::addColumn<QDateTime>("start");

        QTest::newRow("slot start")         << at(25, 6, 30)  << 20.5 << at(25, 6, 30);
        QTest::newRow("inside a slot")      << at(25, 12, 0)  << 18.0 << at(25, 8, 0);
        QTest::newRow("after midnight")     << at(25, 0, 10)  << 17.0 << at(24, 22, 30);
        QTest::newRow("friday to saturday") << at(26, 7, 0)   << 17.0 << at(25, 22, 30);
        QTest::newRow("saturday")           << at(26, 9, 0)   << 20.5 << at(26, 8, 0);
        QTest::newRow("sunday to monday")   << at(28, 5, 0)   << 17.0 << at(27, 23, 0);
    }

    void current()
    {
        QFETCH(QDateTime, now);
        QFETCH(double, temp);
        QFETCH(QDateTime, start);
        const ChronoPoint p = Chrono::current(now, example());
        QVERIFY(p.isValid());
        QCOMPARE(p.temp, temp);
        QCOMPARE(p.start, start);
    }

    void next()
    {
        const ChronoConfig c = example();
        QCOMPARE(Chrono::next(at(25, 5, 0), c).start, at(25, 6, 30));
        QCOMPARE(Chrono::next(at(25, 6, 30), c).temp, 18.0);
        QCOMPARE(Chrono::next(at(25, 22, 30), c).start, at(26, 8, 0));     /* saturday: holiday */
        QCOMPARE(Chrono::next(at(27, 23, 30), c).start, at(28, 6, 30));
    }

    void offAndEmpty()
    {
        ChronoConfig c = example();
        c.enabled = false;
        QVERIFY(!Chrono::current(at(25, 12, 0), c).isValid());
        QVERIFY(!Chrono::next(at(25, 12, 0), c).isValid());
        QCOMPARE(Chrono::dayTemps(QDate(2026, 9, 25), c).size(), 96);      /* still drawn */
        QVERIFY(Chrono::dayTemps(QDate(2026, 9, 25), ChronoConfig()).isEmpty());

        c = example();
        c.holiday.clear();                      /* holidays as weekdays */
        QCOMPARE(Chrono::profileName(QDate(2026, 9, 26), c), QString("weekday"));
        QCOMPARE(Chrono::current(at(26, 7, 0), c).start, at(26, 6, 30));
    }

    void dayTemps()
    {
        const ChronoConfig c = example();
        QVector<double> t = Chrono::dayTemps(QDate(2026, 9, 25), c);   /* Friday */
        QCOMPARE(t.size(), 96);
        QCOMPARE(t[0], 17.0);                   /* previous day's last slot */
        QCOMPARE(t[25], 17.0);                  /* 06:15 */
        QCOMPARE(t[26], 20.5);                  /* 06:30 */
        QCOMPARE(t[32], 18.0);                  /* 08:00 */
        QCOMPARE(t[90], 17.0);                  /* 22:30 */
        QCOMPARE(Chrono::profileName(QDate(2026, 9, 27), c), QString("holiday"));

        t = Chrono::dayTemps(QDate(2026, 9, 28), c, 60);               /* Monday, hourly */
        QCOMPARE(t.size(), 24);
        QCOMPARE(t[6], 17.0);                   /* Sunday 23:00 slot */
        QCOMPARE(t[7], 20.5);
    }

    void editSteps()
    {
        QVector<ChronoSlot> l = makeSlots({ { QTime(6, 40), 20 }, { QTime(8, 0), 18 }, { QTime(8, 15), 19 } });
        QVERIFY(Chrono::stepTime(l, 0, -1));
        QCOMPARE(l[0].at, QTime(6, 30));        /* to the 15-minute grid */
        QVERIFY(Chrono::stepTime(l, 0, +1));
        QCOMPARE(l[0].at, QTime(6, 45));
        QVERIFY(!Chrono::stepTime(l, 1, +1));   /* would reach the next slot */
        QVERIFY(!Chrono::stepTime(l, 2, -1));
        QVERIFY(Chrono::stepTime(l, 2, +1));
        QVERIFY(!Chrono::stepTime(l, 3, +1));   /* no such slot */

        QVector<ChronoSlot> e = makeSlots({ { QTime(0, 0), 20 }, { QTime(23, 45), 17 } });
        QVERIFY(!Chrono::stepTime(e, 0, -1));
        QVERIFY(!Chrono::stepTime(e, 1, +1));   /* not past midnight */

        QVERIFY(Chrono::stepTemp(l, 0, +1));
        QCOMPARE(l[0].temp, 20.5);
        l[1].temp = 5;
        QVERIFY(!Chrono::stepTemp(l, 1, -1));
        l[1].temp = 25;
        QVERIFY(!Chrono::stepTemp(l, 1, +1));
    }

    void splitSlot()
    {
        QVector<ChronoSlot> l;
        QCOMPARE(Chrono::splitSlot(l, 0), 0);   /* empty: 08:00 20 */
        QCOMPARE(Chrono::toString(l), QString("08:00=20.0"));
        QCOMPARE(Chrono::splitSlot(l, 0), 1);   /* last slot: until midnight */
        QCOMPARE(Chrono::toString(l), QString("08:00=20.0, 16:00=20.0"));
        QCOMPARE(Chrono::splitSlot(l, 0), 1);   /* 08:00-16:00 */
        QCOMPARE(Chrono::toString(l), QString("08:00=20.0, 12:00=20.0, 16:00=20.0"));

        l = makeSlots({ { QTime(6, 30), 21 }, { QTime(7, 15), 18 } });
        QCOMPARE(Chrono::splitSlot(l, 0), 1);   /* 45 min: on the grid */
        QCOMPARE(l[1].at, QTime(6, 45));
        QCOMPARE(l[1].temp, 21.0);
        QCOMPARE(Chrono::splitSlot(l, 0), -1);  /* 15 min: too short */
        QCOMPARE(Chrono::splitSlot(l, 5), -1);

        l = makeSlots({ { QTime(23, 30), 17 } });
        QCOMPARE(Chrono::splitSlot(l, 0), 1);
        QCOMPARE(l[1].at, QTime(23, 45));

        l.clear();
        for(int h = 1; h <= ChronoConfig::MAX_SLOTS; h++)
            l.append({ QTime(h * 2, 0), 20 });
        QCOMPARE(Chrono::splitSlot(l, 0), -1);  /* at most 8 */
    }
};

int runChronoTests(int argc, char *argv[])
{
    TstChrono t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_chrono.moc"
