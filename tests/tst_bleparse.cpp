#include <QTest>
#include "bleparse.h"
#include "testutil.h"

using namespace TestUtil;

static const QString MAC1 = "A4:C1:38:0A:0B:0C";
static const QString MAC2 = "A4:C1:38:0D:0E:0F";

static QVector<BleParse::reading_t> parse(const QByteArray &e, unsigned now = 1000)
{
    return BleParse::advertisingReports(reinterpret_cast<const quint8 *>(e.constData()), e.size(), now);
}

class TstBleParse : public QObject
{
    Q_OBJECT

private slots:
    void atc1441()
    {
        const auto r = parse(advEvent({ advReport(MAC1, atcData(MAC1, 204, 48, 85, 2950, 7), -60) }), 1234);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].mac, MAC1);
        QCOMPARE(int(r[0].env.temp), 204);
        QCOMPARE(int(r[0].env.hum), 48);
        QCOMPARE(int(r[0].env.batt), 85);
        QCOMPARE(int(r[0].env.battmv), 2950);
        QCOMPARE(int(r[0].env.pc), 7);
        QCOMPARE(int(r[0].env.rssi), -60);
        QCOMPARE(r[0].env.ldt, 1234u);
    }

    void atc1441Negative()
    {
        auto r = parse(advEvent({ advReport(MAC1, atcData(MAC1, -10, 50, 80, 2900, 1), -70) }));
        QCOMPARE(r.size(), 1);
        QCOMPARE(int(r[0].env.temp), -10);          /* 0xFFF6: -1.0 degC */

        r = parse(advEvent({ advReport(MAC1, atcData(MAC1, -253, 50, 80, 2900, 1), -70) }));
        QCOMPARE(int(r[0].env.temp), -253);
    }

    void pvvx()
    {
        const auto r = parse(advEvent({ advReport(MAC1, pvvxData(MAC1, 2345, 4870, 2950, 85, 9), -55) }));
        QCOMPARE(r.size(), 1);
        QCOMPARE(int(r[0].env.temp), 235);          /* 23.45 -> 23.5 */
        QCOMPARE(int(r[0].env.hum), 49);            /* 48.70 -> 49 */
        QCOMPARE(int(r[0].env.battmv), 2950);
        QCOMPARE(int(r[0].env.batt), 85);
        QCOMPARE(int(r[0].env.pc), 9);
        QCOMPARE(int(r[0].env.rssi), -55);
    }

    void pvvxNegative()
    {
        const auto r = parse(advEvent({ advReport(MAC1, pvvxData(MAC1, -1234, 6000, 2800, 70, 1), -80) }));
        QCOMPARE(r.size(), 1);
        QCOMPARE(int(r[0].env.temp), -123);         /* -12.34 -> -12.3 */
    }

    void flagsBeforeServiceData()
    {
        QByteArray d;
        d.append(char(2)).append(char(0x01)).append(char(0x06));   /* flags */
        d.append(atcData(MAC1, 215, 40, 90, 3000, 3));
        const auto r = parse(advEvent({ advReport(MAC1, d, -50) }));
        QCOMPARE(r.size(), 1);
        QCOMPARE(int(r[0].env.temp), 215);
    }

    void twoReports()
    {
        /* the second report was never read before: the offset did not advance */
        QByteArray other;
        other.append(char(2)).append(char(0x01)).append(char(0x06));
        other.append(char(5)).append(char(0x09)).append("abcd");  /* name */
        const auto r = parse(advEvent({ advReport("11:22:33:44:55:66", other, -90),
                                     advReport(MAC1, atcData(MAC1, 200, 40, 90, 3000, 1), -60),
                                     advReport(MAC2, pvvxData(MAC2, 1950, 5000, 2900, 80, 2), -65) }));
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].mac, MAC1);
        QCOMPARE(int(r[0].env.temp), 200);
        QCOMPARE(int(r[0].env.rssi), -60);
        QCOMPARE(r[1].mac, MAC2);
        QCOMPARE(int(r[1].env.temp), 195);
        QCOMPARE(int(r[1].env.rssi), -65);
    }

    void ignored_data()
    {
        QTest::addColumn<QByteArray>("data");

        /* 0x181A with another length (e.g. the Xiaomi or a 29-byte format) */
        QByteArray other181a = atcData(MAC1, 200, 40, 90, 3000, 1);
        other181a[0] = char(15);
        other181a.chop(1);
        QTest::newRow("unknown 0x181A length") << other181a;

        /* other service UUID, e.g. BTHome 0xFCD2 */
        QByteArray bthome = atcData(MAC1, 200, 40, 90, 3000, 1);
        bthome[2] = char(0xD2);
        bthome[3] = char(0xFC);
        QTest::newRow("other UUID") << bthome;

        /* MAC inside the service data of another device */
        QTest::newRow("atc1441 MAC mismatch") << atcData(MAC2, 200, 40, 90, 3000, 1);
        QTest::newRow("pvvx MAC mismatch")    << pvvxData(MAC2, 2000, 4000, 3000, 90, 1);

        /* AD length beyond the data */
        QByteArray overflow = atcData(MAC1, 200, 40, 90, 3000, 1);
        overflow[0] = char(40);
        QTest::newRow("AD overflow") << overflow;

        QTest::newRow("empty") << QByteArray();

        /* a zero length AD ends the significant part */
        QByteArray zero;
        zero.append(char(0));
        zero.append(atcData(MAC1, 200, 40, 90, 3000, 1));
        QTest::newRow("zero length first") << zero;
    }

    void ignored()
    {
        QFETCH(QByteArray, data);
        QCOMPARE(parse(advEvent({ advReport(MAC1, data, -60) })).size(), 0);
    }

    void truncated()
    {
        /* every prefix of a valid event: no reading, no read past the end */
        const QByteArray e = advEvent({ advReport(MAC1, atcData(MAC1, 200, 40, 90, 3000, 1), -60) });
        for(int n = 0; n < e.size(); n++)
            QCOMPARE(parse(e.left(n)).size(), 0);
        QCOMPARE(parse(e).size(), 1);
    }

    void parameterLengthLimits()
    {
        /* parameter length shorter than the buffer: the report is outside */
        QByteArray e = advEvent({ advReport(MAC1, atcData(MAC1, 200, 40, 90, 3000, 1), -60) });
        e[2] = char(quint8(e.at(2)) - 1);
        QCOMPARE(parse(e).size(), 0);

        /* more reports announced than present */
        e = advEvent({ advReport(MAC1, atcData(MAC1, 200, 40, 90, 3000, 1), -60) });
        e[4] = char(5);
        QCOMPARE(parse(e).size(), 1);
    }

    void otherEvents()
    {
        const QByteArray ok = advEvent({ advReport(MAC1, atcData(MAC1, 200, 40, 90, 3000, 1), -60) });

        QByteArray e = ok;
        e[0] = char(0x02);                          /* ACL data, not an event */
        QCOMPARE(parse(e).size(), 0);

        e = ok;
        e[1] = char(0x0E);                          /* command complete */
        QCOMPARE(parse(e).size(), 0);

        e = ok;
        e[3] = char(0x01);                          /* LE connection complete */
        QCOMPARE(parse(e).size(), 0);
    }

    void normalizeMac_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");

        QTest::newRow("upper")      << "A4:C1:38:0A:0B:0C"      << "A4:C1:38:0A:0B:0C";
        QTest::newRow("lower")      << "a4:c1:38:0a:0b:0c"      << "A4:C1:38:0A:0B:0C";
        QTest::newRow("spaces")     << "  a4:C1:38:0a:0B:0c "   << "A4:C1:38:0A:0B:0C";
        QTest::newRow("dashes")     << "A4-C1-38-0A-0B-0C"      << "";
        QTest::newRow("short")      << "A4:C1:38:0A:0B"         << "";
        QTest::newRow("long")       << "A4:C1:38:0A:0B:0C:0D"   << "";
        QTest::newRow("not hex")    << "G4:C1:38:0A:0B:0C"      << "";
        QTest::newRow("empty")      << ""                       << "";
    }

    void normalizeMac()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(BleParse::normalizeMac(in), out);
    }
};

int runBleParseTests(int argc, char *argv[])
{
    TstBleParse t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_bleparse.moc"
