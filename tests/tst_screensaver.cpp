#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUrl>
#include "mainpage.h"
#include "mainwindow.h"
#include "saverview.h"
#include "screensaver.h"
#include "zonecard.h"

using namespace SaverView;

static zone_t zone(const char *name, ZoneView::Level level, bool heat = false, int batt = 80)
{
    zone_t z;
    z.name = name;
    z.level = level;
    z.heat = heat;
    z.batt = batt;
    return z;
}

class TstScreenSaver : public QObject
{
    Q_OBJECT

private slots:
    void summaryAllOk()
    {
        input_t in;
        in.brokerConnected = in.controllerOnline = true;
        in.zones = { zone("Salotto", ZoneView::Ok, true), zone("Camera", ZoneView::Stale) };
        in.mode.level = "normal";
        const QVector<line_t> l = summary(in);
        QCOMPARE(l.size(), 1);                          /* the rooms heating are icons */
        QCOMPARE(l.at(0).text, QString("All rooms OK"));
        QCOMPARE(l.at(0).level, Ok);
        QCOMPARE(heating(in), QStringList({ "Salotto" }));

        in.zones[0].heat = false;
        QVERIFY(heating(in).isEmpty());
        QCOMPARE(summary(in).at(1).text, QString("No room heating"));
        QCOMPARE(summary(in).at(1).level, Info);
    }

    void summaryAlarms()
    {
        input_t in;
        in.brokerConnected = in.controllerOnline = true;
        in.zones = { zone("Salotto", ZoneView::Lost, true), zone("Camera", ZoneView::Never),
                     zone("Bagno", ZoneView::Ok, true, 15), zone("Studio", ZoneView::Ok, false, 0) };
        in.mode.level = "boost";
        in.mode.text = "Boost";
        in.mode.detail = "29 min left";
        const QVector<line_t> l = summary(in);
        QCOMPARE(l.size(), 4);
        QCOMPARE(l.at(0).text, QString("No sensor: Salotto"));
        QCOMPARE(l.at(0).level, Alarm);
        QCOMPARE(l.at(1).text, QString("No reading yet: Camera"));
        QCOMPARE(l.at(2).text, QString("Battery low: Bagno 15 %"));   /* 0 = unknown: no alarm */
        QCOMPARE(l.at(3).text, QString::fromUtf8("Boost · 29 min left"));
        QCOMPARE(heating(in), QStringList({ "Bagno" }));               /* a lost zone is not heating */

        in.controllerOnline = false;
        QCOMPARE(summary(in).first().text, QString("Heating controller offline"));
        QCOMPARE(summary(in).last().text, QString::fromUtf8("Boost · 29 min left"));   /* no heating line */
        QVERIFY(heating(in).isEmpty());                                /* not known */
        in.brokerConnected = false;
        QCOMPARE(summary(in).first().text, QString("Network offline"));
    }

    void webcamImage()
    {
        /* a file URL: the same download path as http */
        QTemporaryDir dir;
        QImage img(320, 240, QImage::Format_RGB32);
        img.fill(Qt::darkCyan);
        QVERIFY(img.save(dir.filePath("cam.jpg")));

        QVector<sensor_t> sensors(1);
        memset(&sensors[0], 0, sizeof(sensor_t));
        qstrncpy(sensors[0].name, "salotto", sizeof(sensors[0].name));
        qstrncpy(sensors[0].zone, "salotto", sizeof(sensors[0].zone));
        ZoneControl control({ "salotto" });
        screensaver_t cfg;
        cfg.webcamUrl = QUrl::fromLocalFile(dir.filePath("cam.jpg")).toString();
        ScreenSaver saver(sensors, &control, cfg);
        saver.resize(600, 1024);
        saver.show();
        QTRY_VERIFY(!saver.webcamImage().isNull());
        QCOMPARE(saver.webcamImage().size(), QSize(320, 240));
        QVERIFY(!saver.findChild<QLabel *>("saverImage")->pixmap()->isNull());
        QCOMPARE(saver.findChild<QLabel *>("saverLine")->text(), QString("Network offline"));

        /* missing file: the last image stays */
        QVERIFY(QFile::remove(dir.filePath("cam.jpg")));
        saver.hide();
        saver.show();
        QTest::qWait(300);
        QCOMPARE(saver.webcamImage().size(), QSize(320, 240));
    }

    void idleAndWake()
    {
        QVector<sensor_t> sensors(1);
        memset(&sensors[0], 0, sizeof(sensor_t));
        qstrncpy(sensors[0].name, "salotto", sizeof(sensors[0].name));
        qstrncpy(sensors[0].zone, "salotto", sizeof(sensors[0].zone));
        mqtt_brk_t mbi;
        memset(&mbi, 0, sizeof(mbi));
        qstrncpy(mbi.address, "127.0.0.1", sizeof(mbi.address));
        mbi.port = 9;                                   /* nothing there: never connects */
        mbi.publishInterval = 60;
        Mqtt mqtt(&mbi, &sensors);
        mqtt.zoneControl()->setConnected(true);
        mqtt.zoneControl()->onStatus("online");
        mqtt.zoneControl()->onStateTemp("salotto", "20");

        MainWindow w(&mqtt);
        w.setupPages(&sensors);
        w.setIdleMs(300);
        w.showWindow();
        QStackedWidget *stack = w.findChild<QStackedWidget *>();
        MainPage *main = w.findChild<MainPage *>();
        ScreenSaver *saver = w.findChild<ScreenSaver *>();
        QCOMPARE(stack->currentWidget(), main);

        QTRY_COMPARE_WITH_TIMEOUT(stack->currentWidget(), saver, 2000);

        /* a press wakes up the main page */
        QTest::mouseClick(saver, Qt::LeftButton);
        QCOMPARE(stack->currentWidget(), main);
        QTest::qWait(100);
        QCOMPARE(stack->currentWidget(), main);

        /* presses on the page keep it */
        QPushButton *plus = nullptr;
        for(QPushButton *b : main->card(0)->findChildren<QPushButton *>("stepButton"))
            if(b->text() == "+")
                plus = b;
        for(int i = 0; i < 4; i++)
        {
            QTest::qWait(150);
            QTest::mouseClick(plus, Qt::LeftButton);
        }
        QCOMPARE(stack->currentWidget(), main);
        QTRY_COMPARE_WITH_TIMEOUT(stack->currentWidget(), saver, 2000);

        /* the house popup is closed under the screen saver */
        QTest::mouseClick(saver, Qt::LeftButton);
        QTest::mouseClick(main->findChild<QToolButton *>("houseMode"), Qt::LeftButton);
        QWidget *popup = main->findChild<QWidget *>("modeOverlay");
        QVERIFY(popup->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(stack->currentWidget(), saver, 2000);
        QTest::mouseClick(saver, Qt::LeftButton);
        QVERIFY(!popup->isVisible());

        w.setIdleMs(0);                                 /* off */
        QTest::qWait(500);
        QCOMPARE(stack->currentWidget(), main);
    }
};

int runScreenSaverTests(int argc, char *argv[])
{
    TstScreenSaver t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_screensaver.moc"
