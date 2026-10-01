#include <QTest>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include "singleinstance.h"

/* Unique per test run: the abstract socket names are system wide */
static QString lockName(const char *what)
{
    return QString("RoomSense-test-%1-%2").arg(getpid()).arg(what);
}

class TstSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void secondInstanceRefused()
    {
        SingleInstance first(lockName("a"));
        QVERIFY(first.tryLock());
        QVERIFY(first.tryLock());                   /* again: still ours */

        SingleInstance second(lockName("a"));
        QVERIFY(!second.tryLock());

        SingleInstance other(lockName("b"));        /* another name: independent */
        QVERIFY(other.tryLock());
    }

    void freedOnDestruction()
    {
        {
            SingleInstance first(lockName("c"));
            QVERIFY(first.tryLock());
        }
        SingleInstance again(lockName("c"));
        QVERIFY(again.tryLock());
    }

    void freedWhenKilled()
    {
        /* the restart loop must be able to start again after a crash */
        int p[2];
        QCOMPARE(pipe(p), 0);
        const QString name = lockName("d");
        const pid_t child = fork();
        QVERIFY(child >= 0);
        if(child == 0)
        {
            SingleInstance locked(name);
            const char ok = locked.tryLock() ? '1' : '0';
            ssize_t r = write(p[1], &ok, 1);
            (void) r;
            pause();                                /* until SIGKILL */
            _exit(0);
        }
        char ok = 0;
        QCOMPARE(read(p[0], &ok, 1), ssize_t(1));
        QCOMPARE(ok, '1');

        SingleInstance busy(name);
        QVERIFY(!busy.tryLock());

        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        close(p[0]);
        close(p[1]);

        SingleInstance after(name);
        QVERIFY(after.tryLock());
    }
};

int runSingleInstanceTests(int argc, char *argv[])
{
    TstSingleInstance t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_singleinstance.moc"
