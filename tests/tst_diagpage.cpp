#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include "blestatus.h"
#include "diagpage.h"
#include "mqtt.h"

class TstDiagPage : public QObject
{
    Q_OBJECT

private:
    QVector<sensor_t>   m_sensors;
    mqtt_brk_t          m_mbi;
    Mqtt               *m_mqtt = nullptr;
    BleStatus           m_ble;
    DiagPage           *m_page = nullptr;

    QStringList texts(const char *objectName, QWidget *in = nullptr) const
    {
        QStringList l;
        for(QLabel *label : (in ? in : m_page)->findChildren<QLabel *>(objectName))
            l << label->text();
        return l;
    }
    QWidget *card(int index) const
    {
        return m_page->findChildren<QFrame *>("sensorCard").at(index);
    }

private slots:
    void init()
    {
        m_sensors.clear();
        for(const char *z : { "Salotto", "Camera" })
        {
            sensor_t s;
            memset(&s, 0, sizeof(s));
            qstrncpy(s.name, z, sizeof(s.name));
            qstrncpy(s.zone, z, sizeof(s.zone));
            qstrncpy(s.smac, "A4:C1:38:00:00:01", sizeof(s.smac));
            m_sensors.append(s);
        }
        memset(&m_mbi, 0, sizeof(m_mbi));
        qstrncpy(m_mbi.address, "127.0.0.1", sizeof(m_mbi.address));
        m_mbi.port = 9;                                 /* nothing there: never connects */
        m_mbi.publishInterval = 60;
        qstrncpy(m_mbi.uniqueId, "test", sizeof(m_mbi.uniqueId));
        m_mqtt = new Mqtt(&m_mbi, &m_sensors);
        m_page = new DiagPage(&m_sensors, m_mqtt, &m_ble, nullptr);
        m_page->resize(600, 1024);
        m_page->show();
    }

    void cleanup()
    {
        delete m_page;
        delete m_mqtt;
    }

    void system()
    {
        const QStringList values = texts("diagValue");
        QCOMPARE(values.size(), 9);
        QVERIFY(values.at(0).endsWith("127.0.0.1:9"));
        QCOMPARE(values.at(1), QString("RoomSense-test"));
        QCOMPARE(values.at(2), QString("offline"));
        QCOMPARE(values.at(3), QString("starting"));
        QCOMPARE(values.at(4), QString("none yet"));
        QCOMPARE(values.at(5), QString("not active"));

        m_ble.set(BleStatus::Scanning, 1);
        m_ble.advertisement();
        m_page->refresh();
        QCOMPARE(texts("diagValue").at(3), QString("hci1, scanning"));
        QCOMPARE(texts("diagValue").at(4), QString("0 s ago"));
        QCOMPARE(m_page->findChildren<QLabel *>("diagDot").at(3)->property("level").toString(), QString("ok"));
        QCOMPARE(m_page->findChildren<QLabel *>("diagDot").at(5)->property("level").toString(), QString("warn"));
    }

    void sensors()
    {
        QCOMPARE(m_page->findChildren<QFrame *>("sensorCard").size(), 2);
        QCOMPARE(texts("sensorName", card(0)), QStringList({ "Salotto" }));
        QCOMPARE(texts("sensorMac", card(0)), QStringList({ "A4:C1:38:00:00:01" }));
        QCOMPARE(texts("sensorValue", card(0)), QStringList({ "--", "never", "0", "0" }));
        QCOMPARE(card(0)->property("level").toString(), QString("never"));

        sensor_env_t e;
        memset(&e, 0, sizeof(e));
        e.temp = 213;
        e.rssi = -71;
        e.ldt = unsigned(QDateTime::currentSecsSinceEpoch());
        m_page->onReading(1, e.ldt, e);
        m_page->refresh();
        QCOMPARE(texts("sensorValue", card(1)).at(0), QString("-71 dBm"));
        QCOMPARE(texts("sensorValue", card(1)).at(1), QDateTime::fromSecsSinceEpoch(e.ldt).toString("HH:mm:ss"));
        QCOMPARE(card(1)->property("level").toString(), QString("ok"));
        QCOMPARE(card(0)->property("level").toString(), QString("never"));
    }

    void tabsAndBack()
    {
        QTabWidget *tabs = m_page->findChild<QTabWidget *>("diagTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tabText(0), QString("System"));
        QCOMPARE(tabs->tabText(1), QString("Sensors"));
        QCOMPARE(tabs->tabText(2), QString("Log"));
        tabs->setCurrentIndex(2);
        QVERIFY(!m_page->findChild<QPlainTextEdit *>("logBox")->toPlainText().isEmpty());
        QVERIFY(!m_page->findChild<QPushButton *>("hideButton"));

        QSignalSpy back(m_page, &DiagPage::backRequested);
        QTest::mouseClick(m_page->findChild<QPushButton *>("backButton"), Qt::LeftButton);
        QCOMPARE(back.count(), 1);
    }
};

int runDiagPageTests(int argc, char *argv[])
{
    TstDiagPage t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_diagpage.moc"
