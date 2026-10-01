#include <QTest>
#include <QTemporaryDir>
#include "heartbeat.h"
#include "monoclock.h"
#include "watchdog.h"

/* Messages of the watchdog, captured while a test runs */
static QStringList s_messages;
static void capture(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    s_messages.append(msg);
}

static int count(const QString &text)
{
    int n = 0;
    for(const QString &m : s_messages)
        n += m.contains(text) ? 1 : 0;
    return n;
}

class TstWatchDog : public QObject
{
    Q_OBJECT

private:
    QtMessageHandler m_previous = nullptr;

private slots:
    void init()
    {
        s_messages.clear();
        m_previous = qInstallMessageHandler(capture);
    }

    void cleanup()
    {
        qInstallMessageHandler(m_previous);
    }

    void heartbeat()
    {
        Heartbeat h;
        QVERIFY(h.isAlive(30000));
        MonoClock::advanceForTest(31000);
        QVERIFY(!h.isAlive(30000));             /* stalled thread */
        h.beat();
        QVERIFY(h.isAlive(30000));
    }

    void missingDevice()
    {
        WatchDog w;
        QVERIFY(!w.start("/nonexistent/watchdog"));
        QVERIFY(!w.isActive());
        w.stop();                               /* harmless */
    }

    void healthCheck()
    {
        /* a regular file instead of /dev/watchdog: the keepalive ioctl
         * fails and says so, which shows that a refresh was attempted */
        QTemporaryDir dir;
        const QString dev = dir.filePath("watchdog");
        QFile f(dev);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();

        bool healthy = false;
        WatchDog w;
        w.setHealthCheck([&healthy] { return healthy; });
        QVERIFY(w.start(dev));                  /* refreshes at once */
        QVERIFY(w.isActive());
        QCOMPARE(count("Err ioctl WDIOC_KEEPALIVE"), 0);
        QCOMPARE(count("watchdog not refreshed"), 1);

        QMetaObject::invokeMethod(&w, "refresh");
        QCOMPARE(count("Err ioctl WDIOC_KEEPALIVE"), 0);
        QCOMPARE(count("watchdog not refreshed"), 1);   /* said once */

        healthy = true;
        QMetaObject::invokeMethod(&w, "refresh");
        QCOMPARE(count("refreshed again"), 1);
        QCOMPARE(count("Err ioctl WDIOC_KEEPALIVE"), 1);

        w.setHealthCheck(nullptr);              /* shutdown: no check */
        healthy = false;
        QMetaObject::invokeMethod(&w, "refresh");
        QCOMPARE(count("Err ioctl WDIOC_KEEPALIVE"), 2);

        /* clean exit: magic close */
        w.stop();
        QVERIFY(!w.isActive());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("V"));
    }
};

int runWatchDogTests(int argc, char *argv[])
{
    TstWatchDog t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_watchdog.moc"
