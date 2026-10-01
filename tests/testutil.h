#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QTemporaryDir>
#include <QTextStream>

namespace TestUtil
{
    /* 6 bytes of "AA:BB:CC:DD:EE:FF", most significant first */
    inline QByteArray macBytes(const QString &mac)
    {
        return QByteArray::fromHex(mac.toLatin1().replace(":", ""));
    }

    inline QByteArray reversed(const QByteArray &b)
    {
        QByteArray r;
        for(int i = b.size() - 1; i >= 0; i--)
            r.append(b.at(i));
        return r;
    }

    inline void be16(QByteArray &b, int v) { b.append(char((v >> 8) & 0xFF)).append(char(v & 0xFF)); }
    inline void le16(QByteArray &b, int v) { b.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)); }

    /* Service data 0x181A, atc1441 format (17 bytes) */
    inline QByteArray atcData(const QString &mac, int temp, int hum, int batt, int mv, int counter)
    {
        QByteArray d;
        d.append(char(16)).append(char(0x16)).append(char(0x1A)).append(char(0x18));
        d.append(macBytes(mac));
        be16(d, temp);
        d.append(char(hum)).append(char(batt));
        be16(d, mv);
        d.append(char(counter));
        return d;
    }

    /* Service data 0x181A, pvvx custom format (19 bytes); temp and hum in 0.01 */
    inline QByteArray pvvxData(const QString &mac, int temp, int hum, int mv, int batt, int counter)
    {
        QByteArray d;
        d.append(char(18)).append(char(0x16)).append(char(0x1A)).append(char(0x18));
        d.append(reversed(macBytes(mac)));
        le16(d, temp);
        le16(d, hum);
        le16(d, mv);
        d.append(char(batt)).append(char(counter)).append(char(0x05));
        return d;
    }

    /* One advertising report: event type, address type, address, data, RSSI */
    inline QByteArray advReport(const QString &mac, const QByteArray &data, int rssi)
    {
        QByteArray r;
        r.append(char(0x00)).append(char(0x00));
        r.append(reversed(macBytes(mac)));
        r.append(char(data.size()));
        r.append(data);
        r.append(char(rssi));
        return r;
    }

    /* HCI LE Advertising Report event, as read from the HCI socket */
    inline QByteArray advEvent(const QList<QByteArray> &reports)
    {
        QByteArray p;
        p.append(char(0x02)).append(char(reports.size()));
        for(const QByteArray &r : reports)
            p.append(r);
        QByteArray e;
        e.append(char(0x04)).append(char(0x3E)).append(char(p.size()));
        return e + p;
    }

    /* setting.ini in a temporary dir */
    inline QString writeIni(const QTemporaryDir &dir, const QString &content, const QString &name = "setting.ini")
    {
        const QString path = dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        QTextStream(&f) << content;
        return path;
    }
}

#endif // TESTUTIL_H
