#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include "chronopage.h"
#include "chronoview.h"
#include "daychart.h"
#include "mainpage.h"
#include "zonecard.h"

/* the program of the interface example; 2026-10-06 is a Tuesday */
static const char *PROGRAM =
    R"({"edited":true,"enabled":true,"holiday":[{"at":"08:00","temp":20.5},{"at":"23:00","temp":17}],)"
    R"("holiday_days":[6,7],"weekday":[{"at":"06:30","temp":20.5},{"at":"08:00","temp":18},)"
    R"({"at":"17:00","temp":20.5},{"at":"22:30","temp":17}]})";
static const char *STATE_ON =
    R"({"chrono":"on","manual":false,"next_change":"22:30","next_setpoint":17,"paused":false,"profile":"weekday"})";
static const QDate TUESDAY(2026, 10, 6);

static ZoneControl::chrono_t chrono(const char *state = STATE_ON)
{
    ZoneControl::chrono_t c;
    c.hasProgram = Chrono::parseProgram(PROGRAM, c.program);
    c.hasState = Chrono::parseState(state, c.state);
    return c;
}

class TstChronoPage : public QObject
{
    Q_OBJECT

private:
    QVector<sensor_t>   m_sensors;
    ZoneControl        *m_control = nullptr;

private slots:
    void view()
    {
        const ChronoView::view_t v = ChronoView::make(chrono(), true, TUESDAY);
        QVERIFY(v.hasProgram);
        QCOMPARE(v.title, QString::fromUtf8("Today · weekday"));
        QCOMPARE(v.next, QString::fromUtf8("next: 22:30 → 17.0°"));
        QCOMPARE(v.week.size(), 7);
        QCOMPARE(v.week.at(0).label, QString("Today"));
        QCOMPARE(v.week.at(1).label, QString("Wed 7"));
        QCOMPARE(v.week.at(0).temps.size(), 96);
        QCOMPARE(v.week.at(0).range, QString::fromUtf8("17.0 – 20.5"));
        QVERIFY(!v.week.at(0).holiday);
        QVERIFY(v.week.at(4).holiday);                  /* Saturday */
        QCOMPARE(v.weekday, QStringList({ QString::fromUtf8("06:30   20.5°"), QString::fromUtf8("08:00   18.0°"),
                                          QString::fromUtf8("17:00   20.5°"), QString::fromUtf8("22:30   17.0°") }));
        QCOMPARE(v.holidayTitle, QString::fromUtf8("HOLIDAY · SAT SUN"));
        QCOMPARE(v.holiday.size(), 2);
        QCOMPARE(v.footer, QString("Program edited on a panel"));
        QCOMPARE(v.toggleText, QString("Chrono ON"));
        QVERIFY(v.toggleOn);
        QVERIFY(v.toggleEnabled);
        QVERIFY(v.chartOn);
    }

    void viewStates()
    {
        QCOMPARE(ChronoView::make(chrono(R"({"chrono":"on","manual":true,"next_change":"22:30","next_setpoint":17,"paused":false})"), true, TUESDAY).next,
                 QString::fromUtf8("manual setpoint, next: 22:30 → 17.0°"));
        QCOMPARE(ChronoView::make(chrono(R"({"chrono":"on","manual":false,"paused":true})"), true, TUESDAY).next,
                 QString("chrono paused (away)"));
        ChronoView::view_t v = ChronoView::make(chrono(R"({"chrono":"off"})"), true, TUESDAY);
        QCOMPARE(v.next, QString("chrono off"));
        QCOMPARE(v.toggleText, QString("Chrono OFF"));
        QVERIFY(!v.chartOn);
        QCOMPARE(v.week.at(0).temps.size(), 96);        /* the program still drawn */

        ZoneControl::chrono_t c = chrono();
        c.saving = true;
        v = ChronoView::make(c, false, TUESDAY);
        QCOMPARE(v.toggleText, QString::fromUtf8("Saving…"));
        QVERIFY(!v.toggleEnabled);

        /* off without weekday slots: cannot be turned on */
        c = chrono(R"({"chrono":"off"})");
        c.program.weekday.clear();
        c.program.edited = false;
        v = ChronoView::make(c, true, TUESDAY);
        QVERIFY(!v.toggleEnabled);
        QCOMPARE(v.footer, QString("Program of the controller configuration"));

        QVERIFY(!ChronoView::make(ZoneControl::chrono_t(), true, TUESDAY).hasProgram);
    }

    void texts()
    {
        QCOMPARE(ChronoView::range({ 20, 20 }), QString("20.0"));
        QCOMPARE(ChronoView::daysText(0), QString("none"));
        QCOMPARE(ChronoView::daysText((1 << 1) | (1 << 7)), QString("MON SUN"));
        QCOMPARE(ChronoView::resultText(ZoneControl::ChronoSaved), QString("Saved"));
        QCOMPARE(ChronoView::resultText(ZoneControl::ChronoRefused), QString("Not saved: refused by the controller"));
        QCOMPARE(ChronoView::resultText(ZoneControl::ChronoNotConfirmed), QString("Not saved: no answer from the controller"));
    }

    void init()
    {
        m_sensors.clear();
        for(const char *z : { "salotto", "camera" })
        {
            sensor_t s;
            memset(&s, 0, sizeof(s));
            qstrncpy(s.name, z, sizeof(s.name));
            qstrncpy(s.zone, z, sizeof(s.zone));
            m_sensors.append(s);
        }
        m_control = new ZoneControl({ "salotto", "camera" }, 50, 300);
        m_control->setConnected(true);
        m_control->onStatus("online");
    }

    void cleanup()
    {
        delete m_control;
    }

    void page()
    {
        ChronoPage page(m_sensors, m_control);
        page.resize(600, 1024);
        page.show();
        page.showZone(0);
        QCOMPARE(page.findChild<QLabel *>("chronoTitle")->text(), QString("salotto"));
        QVERIFY(page.findChild<QLabel *>("chronoNone")->isVisible());   /* no program yet */
        QPushButton *toggle = page.findChild<QPushButton *>("chronoToggle");
        QVERIFY(!toggle->isVisible());

        m_control->onChronoState("salotto", STATE_ON);
        m_control->onChronoProgram("salotto", PROGRAM);
        QVERIFY(!page.findChild<QLabel *>("chronoNone")->isVisible());
        QVERIFY(toggle->isVisible());
        QCOMPARE(toggle->text(), QString("Chrono ON"));
        QCOMPARE(page.findChild<QLabel *>("chronoNext")->text(), QString::fromUtf8("next: 22:30 → 17.0°"));
        QCOMPARE(page.findChildren<DayChart *>().size(), 8);           /* today and the week */
        QCOMPARE(page.findChildren<DayChart *>().first()->temps().size(), 96);

        /* on/off through ZoneControl, then the answer */
        QSignalSpy sent(m_control, &ZoneControl::sendChronoOn);
        QTest::mouseClick(toggle, Qt::LeftButton);
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(1).toBool(), false);
        QCOMPARE(toggle->text(), QString::fromUtf8("Saving…"));
        QVERIFY(!toggle->isEnabled());
        m_control->onChronoState("salotto", R"({"chrono":"off"})");
        QCOMPARE(toggle->text(), QString("Chrono OFF"));
        QLabel *message = page.findChild<QLabel *>("chronoMessage");
        QVERIFY(message->isVisible());
        QCOMPARE(message->text(), QString("Saved"));

        /* a change of another zone does not touch the page */
        m_control->onChronoState("camera", STATE_ON);
        QCOMPARE(toggle->text(), QString("Chrono OFF"));

        QSignalSpy back(&page, &ChronoPage::backRequested);
        QTest::mouseClick(page.findChild<QPushButton *>("backButton"), Qt::LeftButton);
        QCOMPARE(back.count(), 1);
    }

    void cardTap()
    {
        MainPage main(m_sensors, m_control);
        main.resize(600, 1024);
        main.show();
        QSignalSpy requested(&main, &MainPage::chronoRequested);
        QPushButton *line = main.card(0)->findChild<QPushButton *>("chronoButton");
        m_control->onChronoState("salotto", STATE_ON);
        QTest::mouseClick(line, Qt::LeftButton);
        QCOMPARE(requested.count(), 0);                 /* no program: nothing to show */
        m_control->onChronoProgram("salotto", PROGRAM);
        QTest::mouseClick(main.card(0), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(requested.count(), 0);                 /* the rest of the card: nothing */
        QTest::mouseClick(line, Qt::LeftButton);
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.at(0).at(0).toInt(), 0);
    }
};

int runChronoPageTests(int argc, char *argv[])
{
    TstChronoPage t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_chronopage.moc"
