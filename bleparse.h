#ifndef BLEPARSE_H
#define BLEPARSE_H

#include "common.h"

/*
 * Decoding of the BLE advertisements of the room sensors (Xiaomi LYWSD03MMC
 * and similar with the custom firmware of atc1441 or pvvx), independent of
 * BlueZ: works on the raw bytes read from the HCI socket, unit tested on
 * the desktop.
 *
 * Accepted: service data 0x181A in one of the two documented formats,
 * with the MAC inside matching the advertiser address.
 *   atc1441  AD length 16: MAC (big endian), temperature int16 BE 0.1 degC,
 *            humidity %, battery %, battery mV uint16 BE, frame counter
 *   pvvx     AD length 18: MAC (little endian), temperature int16 LE
 *            0.01 degC, humidity uint16 LE 0.01 %, battery mV uint16 LE,
 *            battery %, counter, flags
 * Anything else is ignored: a wrong reading is worse than a missing one.
 */
namespace BleParse
{
    typedef struct {
        QString      mac;   /*AA:BB:CC:DD:EE:FF, upper case*/
        sensor_env_t env;
    } reading_t;

    /* HCI LE Advertising Report event as read from the HCI socket (packet
     * type first): the readings of the sensor advertisements it contains.
     * now: timestamp stored in env.ldt */
    QVector<reading_t> advertisingReports(const quint8 *buf, int len, unsigned now);

    /* Advertising data of one advertisement from mac: true and env filled
     * (temp, hum, batt, battmv, pc) if it holds a sensor reading */
    bool decodeEnv(const QString &mac, const quint8 *data, int len, sensor_env_t &env);

    /* "a4:c1:38:0a:0b:0c " -> "A4:C1:38:0A:0B:0C"; empty if not a MAC */
    QString normalizeMac(const QString &mac);
}

#endif // BLEPARSE_H
