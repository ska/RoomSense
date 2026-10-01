#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTest>
#include "mainpage.h"
#include "mainwindow.h"

class TstMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void pages()
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

        MainWindow w(&mqtt);
        w.setupPages(&sensors);
        w.showWindow();
        QStackedWidget *stack = w.findChild<QStackedWidget *>();
        MainPage *main = w.findChild<MainPage *>();
        QVERIFY(stack && main);
        QCOMPARE(stack->currentWidget(), main);         /* starts on the main page */

        /* long press on the clock: diagnostic page */
        QWidget *clock = main->findChild<QLabel *>("clockLabel")->parentWidget();
        QTest::mousePress(clock, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(stack->currentWidget() != main, 2000);
        QTest::mouseRelease(clock, Qt::LeftButton);
        QCOMPARE(stack->currentWidget()->objectName(), QString("diagPage"));

        /* Back: main page again */
        QPushButton *back = stack->currentWidget()->findChild<QPushButton *>("backButton");
        QVERIFY(back);
        QTest::mouseClick(back, Qt::LeftButton);
        QCOMPARE(stack->currentWidget(), main);
    }
};

int runMainWindowTests(int argc, char *argv[])
{
    TstMainWindow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_mainwindow.moc"
