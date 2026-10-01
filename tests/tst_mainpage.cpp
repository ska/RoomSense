#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include "mainpage.h"
#include "modepopup.h"
#include "zonecard.h"

class TstMainPage : public QObject
{
    Q_OBJECT

private:
    QVector<sensor_t>   m_sensors;
    ZoneControl        *m_control = nullptr;
    MainPage           *m_page = nullptr;

    QList<QPushButton *> buttons(int index) const
    {
        return m_page->card(index)->findChildren<QPushButton *>("stepButton");
    }
    QString text(int index, const char *label) const
    {
        return m_page->card(index)->findChild<QLabel *>(label)->text();
    }

private slots:
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
        m_control->onStateTemp("salotto", "20");
        m_page = new MainPage(m_sensors, m_control);
        m_page->resize(600, 1024);
        m_page->show();
    }

    void cleanup()
    {
        delete m_page;
        delete m_control;
    }

    void reading()
    {
        QCOMPARE(text(0, "tempLabel"), QString("--.-°"));
        QCOMPARE(text(0, "statusLabel"), QString("no reading yet"));
        sensor_env_t e;
        memset(&e, 0, sizeof(e));
        e.temp = 213;
        e.hum = 48;
        e.batt = 15;
        m_page->onReading(0, 0, e);
        QCOMPARE(text(0, "tempLabel"), QString("21.3°"));
        QCOMPARE(text(0, "battLabel"), QString("Batt 15 % low"));
        QCOMPARE(m_page->card(0)->findChild<QLabel *>("battLabel")->property("low").toString(), QString("true"));
    }

    void plusSendsOneCommand()
    {
        QSignalSpy sent(m_control, &ZoneControl::sendSetPoint);
        const QList<QPushButton *> b = buttons(0);
        QCOMPARE(b.size(), 2);
        QPushButton *plus = b.at(0)->text() == "+" ? b.at(0) : b.at(1);
        QVERIFY(plus->isEnabled());
        QTest::mouseClick(plus, Qt::LeftButton);
        QTest::mouseClick(plus, Qt::LeftButton);
        QCOMPARE(text(0, "setPointValue"), QString("21.0"));
        QCOMPARE(text(0, "setPointLabel"), QString("sending…"));
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(1).toDouble(), 21.0);
        m_control->onStateTemp("salotto", "21");
        QCOMPARE(text(0, "setPointLabel"), QString("setpoint"));
    }

    void noSetPointYet()
    {
        for(QPushButton *b : buttons(1))                /* camera: no state_temp */
            QVERIFY(!b->isEnabled());
        QCOMPARE(text(1, "setPointValue"), QString("--.-"));
    }

    void controllerOffline()
    {
        QLabel *banner = m_page->findChild<QLabel *>("offlineBanner");
        QVERIFY(!banner->isVisible());
        m_control->onStatus("offline");
        QVERIFY(banner->isVisible());
        for(QPushButton *b : buttons(0))
            QVERIFY(!b->isEnabled());
        QCOMPARE(text(0, "setPointLabel"), QString("last known"));
    }

    void houseModePopup()
    {
        QToolButton *bar = m_page->findChild<QToolButton *>("houseMode");
        QWidget *popup = m_page->findChild<QWidget *>("modeOverlay");
        QVERIFY(bar && popup);
        QCOMPARE(bar->text(), QString("Unknown"));
        m_control->onModeState(R"({"mode":"normal"})");
        QCOMPARE(bar->text(), QString("Normal"));

        QTest::mouseClick(bar, Qt::LeftButton);
        QVERIFY(popup->isVisible());
        QList<QToolButton *> modes = popup->findChildren<QToolButton *>("modeButton");
        QCOMPARE(modes.size(), 3);                          /* boost, windows open, away */
        for(QToolButton *b : modes)
            QVERIFY(b->isEnabled());

        QSignalSpy sent(m_control, &ZoneControl::sendMode);
        QTest::mouseClick(modes.at(2), Qt::LeftButton);    /* away */
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(0).toString(), QString("away"));
        QVERIFY(!popup->isVisible());
        QCOMPARE(bar->text(), QString::fromUtf8("Sending…"));

        m_control->onModeState(R"({"mode":"away"})");
        QCOMPARE(bar->text(), QString("Away"));
        QCOMPARE(bar->property("level").toString(), QString("away"));
        for(QPushButton *b : buttons(0))                    /* -/+ disabled */
            QVERIFY(!b->isEnabled());

        QTest::mouseClick(bar, Qt::LeftButton);
        QVERIFY(!modes.at(1)->isEnabled());                 /* no windows open while away */
        QVERIFY(modes.at(0)->isEnabled());                  /* boost ends away */
        QCOMPARE(modes.at(2)->property("active").toString(), QString("true"));
        QTest::mouseClick(modes.at(2), Qt::LeftButton);    /* active one: back to normal */
        QCOMPARE(sent.at(1).at(0).toString(), QString("normal"));

        m_control->onModeState(R"({"mode":"boost","remaining_s":1800})");
        QCOMPARE(bar->text(), QString("Boost\n30 min left"));
        QCOMPARE(bar->property("level").toString(), QString("boost"));
        QCOMPARE(text(0, "setPointLabel"), QString("boost"));
        QTest::mouseClick(bar, Qt::LeftButton);
        QCOMPARE(modes.at(0)->property("active").toString(), QString("true"));
        QTest::mouseClick(modes.at(1), Qt::LeftButton);    /* windows open replace boost */
        QCOMPARE(sent.at(2).at(0).toString(), QString("window"));

        QTest::mouseClick(bar, Qt::LeftButton);             /* a press outside closes */
        QVERIFY(popup->isVisible());
        QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QVERIFY(!popup->isVisible());
    }

    void allRooms()
    {
        m_control->onStateTemp("camera", "18");             /* salotto 20 */
        m_control->onModeState(R"({"mode":"normal"})");
        QTest::mouseClick(m_page->findChild<QToolButton *>("houseMode"), Qt::LeftButton);
        QWidget *popup = m_page->findChild<QWidget *>("modeOverlay");
        QLabel *value = popup->findChild<QLabel *>("allValue");
        QCOMPARE(value->text(), QString::fromUtf8("18.0° – 20.0°"));
        QList<QPushButton *> b = popup->findChildren<QPushButton *>("stepButton");
        QCOMPARE(b.size(), 2);
        QPushButton *minus = b.at(0)->text() == "+" ? b.at(1) : b.at(0);

        QSignalSpy sent(m_control, &ZoneControl::sendSetPoint);
        QTest::mouseClick(minus, Qt::LeftButton);
        QCOMPARE(value->text(), QString::fromUtf8("18.0°"));
        QVERIFY(popup->isVisible());                        /* stays open */
        QTest::mouseClick(minus, Qt::LeftButton);
        QCOMPARE(value->text(), QString::fromUtf8("17.5°"));
        QCOMPARE(text(0, "setPointValue"), QString("17.5"));
        QCOMPARE(text(0, "setPointLabel"), QString::fromUtf8("sending…"));
        QTRY_COMPARE(sent.count(), 2);

        m_control->onModeState(R"({"mode":"boost","remaining_s":60})");
        QVERIFY(!minus->isEnabled());                       /* not during a house mode */
    }

    void popupClosesWhenIdle()
    {
        ModePopup *popup = m_page->findChild<ModePopup *>();
        popup->setIdleCloseMs(400);
        m_control->onModeState(R"({"mode":"normal"})");
        QTest::mouseClick(m_page->findChild<QToolButton *>("houseMode"), Qt::LeftButton);
        QVERIFY(popup->isVisible());
        QTest::qWait(250);
        QPushButton *minus = popup->findChildren<QPushButton *>("stepButton").first();
        QTest::mouseClick(minus, Qt::LeftButton);          /* a press restarts the time */
        QTest::qWait(250);
        QVERIFY(popup->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!popup->isVisible(), 1000);
    }

    void longPressOnClock()
    {
        QSignalSpy diag(m_page, &MainPage::diagnosticsRequested);
        QWidget *clock = m_page->findChild<QLabel *>("clockLabel")->parentWidget();

        QTest::mousePress(clock, Qt::LeftButton);       /* short press: nothing */
        QTest::qWait(300);
        QTest::mouseRelease(clock, Qt::LeftButton);
        QTest::qWait(900);
        QCOMPARE(diag.count(), 0);

        QTest::mousePress(clock, Qt::LeftButton);       /* held */
        QTRY_COMPARE_WITH_TIMEOUT(diag.count(), 1, 2000);
        QTest::mouseRelease(clock, Qt::LeftButton);
    }
};

int runMainPageTests(int argc, char *argv[])
{
    TstMainPage t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mainpage.moc"
