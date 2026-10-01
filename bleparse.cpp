#include "bleparse.h"
#include <QRegularExpression>

/* HCI (Bluetooth Core, Vol 4 Part E) */
#define HCI_PKT_EVENT           0x04
#define HCI_EVT_LE_META         0x3E
#define HCI_LE_ADV_REPORT       0x02
#define ADV_REPORT_HDR_SIZE     9       /*event type, address type, address[6], data length*/

/* Advertising data */
#define AD_TYPE_SERVICE_DATA16  0x16
#define UUID_ENV_SENSING        0x181A
#define AD_LEN_ATC1441          16
#define AD_LEN_PVVX             18

static quint16 be16(const quint8 *p) { return quint16(p[0] << 8 | p[1]); }
static quint16 le16(const quint8 *p) { return quint16(p[1] << 8 | p[0]); }

/* MAC string of 6 bytes, most significant first */
static QString macString(const quint8 *b, bool reversed)
{
    QString s;
    for(int i = 0; i < 6; i++)
    {
        if(i)
            s += ':';
        s += QString("%1").arg(b[reversed ? 5 - i : i], 2, 16, QLatin1Char('0')).toUpper();
    }
    return s;
}

QString BleParse::normalizeMac(const QString &mac)
{
    static const QRegularExpression re("^([0-9A-F]{2}:){5}[0-9A-F]{2}$");
    const QString m = mac.trimmed().toUpper();
    return re.match(m).hasMatch() ? m : QString();
}

bool BleParse::decodeEnv(const QString &mac, const quint8 *data, int len, sensor_env_t &env)
{
    int i = 0;
    while(i < len)
    {
        const int adLen = data[i];
        if(adLen == 0)
            break;                              /*end of the significant part*/
        if(i + 1 + adLen > len)
            return false;                       /*malformed*/

        const quint8 *ad = data + i + 1;        /*type, then adLen - 1 bytes*/
        if(ad[0] == AD_TYPE_SERVICE_DATA16 && adLen >= 3 && le16(ad + 1) == UUID_ENV_SENSING)
        {
            const quint8 *p = ad + 3;           /*after the UUID*/
            sensor_env_t e;
            memset(&e, 0, sizeof(e));

            if(adLen == AD_LEN_ATC1441)
            {
                if(macString(p, false) != mac)
                    return false;
                e.temp   = qint16(be16(p + 6));
                e.hum    = p[8];
                e.batt   = p[9];
                e.battmv = be16(p + 10);
                e.pc     = p[12];
            }
            else if(adLen == AD_LEN_PVVX)
            {
                if(macString(p, true) != mac)
                    return false;
                e.temp   = qint16(qRound(qint16(le16(p + 6)) / 10.0));
                e.hum    = quint8(qBound(0, qRound(le16(p + 8) / 100.0), 255));
                e.battmv = le16(p + 10);
                e.batt   = p[12];
                e.pc     = p[13];
            }
            else
                return false;                   /*unknown format of 0x181A*/

            env = e;
            return true;
        }
        i += 1 + adLen;
    }
    return false;
}

QVector<BleParse::reading_t> BleParse::advertisingReports(const quint8 *buf, int len, unsigned now)
{
    QVector<reading_t> out;

    /* packet type, event code, parameter length, subevent, number of reports */
    if(len < 5 || buf[0] != HCI_PKT_EVENT || buf[1] != HCI_EVT_LE_META)
        return out;
    const int end = qMin(len, 3 + int(buf[2]));
    if(end < 5 || buf[3] != HCI_LE_ADV_REPORT)
        return out;

    /* reports one after the other, as the Linux kernel reads them */
    int reports = buf[4];
    int p = 5;
    while(reports-- > 0)
    {
        if(p + ADV_REPORT_HDR_SIZE > end)
            break;
        const quint8 *addr = buf + p + 2;       /*little endian*/
        const int dataLen = buf[p + 8];
        const quint8 *data = buf + p + ADV_REPORT_HDR_SIZE;
        if(p + ADV_REPORT_HDR_SIZE + dataLen + 1 > end)
            break;                              /*truncated: data and RSSI*/

        reading_t r;
        r.mac = macString(addr, true);
        if(decodeEnv(r.mac, data, dataLen, r.env))
        {
            r.env.rssi = qint8(data[dataLen]);
            r.env.ldt  = now;
            out.append(r);
        }
        p += ADV_REPORT_HDR_SIZE + dataLen + 1;
    }
    return out;
}
