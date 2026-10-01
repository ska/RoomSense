#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QRegularExpression>
#include "logging.h"

class TstLogging : public QObject
{
    Q_OBJECT

private slots:
    void rotation()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("log/test.log");
        Logging::install();
        Logging::enableFile(path, 16 * 1024, 2);

        /* ~60 KB of info: rotated, only 2 old files kept */
        const QString filler(100, 'x');
        for(int i = 0; i < 600; i++)
            qCInfo(lcApp) << i << filler;
        qCDebug(lcApp) << "debug is not written";

        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(path + ".1"));
        QVERIFY(QFile::exists(path + ".2"));
        QVERIFY(!QFile::exists(path + ".3"));
        QVERIFY(QFileInfo(path).size() <= 16 * 1024 + 200);

        QFile f(path);
        f.open(QIODevice::ReadOnly);
        const QByteArray last = f.readAll();
        QVERIFY(last.contains("rs.app: 599"));
        QVERIFY(!last.contains("debug is not written"));
        QCOMPARE(Logging::filePath(), path);
    }

    void recentMessages()
    {
        Logging::install();
        const quint64 before = Logging::recentCount();
        qCDebug(lcApp) << "debug is not kept";
        QCOMPARE(Logging::recentCount(), before);

        qCInfo(lcApp).noquote() << "some info";
        qCWarning(lcMqtt).noquote() << "first warning";
        qCCritical(lcBle).noquote() << "an error";
        QCOMPARE(Logging::recentCount(), before + 3);
        const QStringList r = Logging::recent();
        QVERIFY(r.at(0).endsWith(" C rs.ble: an error"));        /* newest first */
        QVERIFY(r.at(1).endsWith(" W rs.mqtt: first warning"));
        QVERIFY(r.at(2).endsWith(" I rs.app: some info"));
        QVERIFY(QRegularExpression("^\\d\\d:\\d\\d:\\d\\d W ").match(r.at(1)).hasMatch());

        for(int i = 0; i < Logging::RECENT_MAX + 10; i++)
            qCWarning(lcApp) << "warning" << i;
        QCOMPARE(Logging::recent().size(), Logging::RECENT_MAX);
        QVERIFY(Logging::recent().first().endsWith(QString("warning %1").arg(Logging::RECENT_MAX + 9)));
    }
};

int runLoggingTests(int argc, char *argv[])
{
    TstLogging t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_logging.moc"
