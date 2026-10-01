#ifndef BLESENSORS_H
#define BLESENSORS_H

#include "common.h"
#include "blestatus.h"
#include "heartbeat.h"
#include <atomic>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

Q_DECLARE_METATYPE(sensor_t)
Q_DECLARE_METATYPE(sensor_env_t)

/*
 * BLE scanner thread: reads the advertisements from the HCI socket and
 * emits the readings of the configured sensors (decoding: bleparse.h).
 * Never exits on errors: the HCI device is reopened with a backoff, and
 * also when no advertisement at all arrives for NO_EVENT_REINIT_S.
 */
class BleSensors : public QThread
{
    Q_OBJECT
public:
    explicit BleSensors(QVector<sensor_t> *sensors = nullptr);
    void stop();
    /* Beats at least every second while the thread runs (watchdog) */
    const Heartbeat &heartbeat() const { return m_heartbeat; }
    /* Adapter state and last advertisement (diagnostic page) */
    const BleStatus &status() const { return m_status; }
    struct hci_request ble_hci_request(uint16_t ocf, int clen, void * status, void * cparam);

signals:
    void sensorEnvValueChanged( int index, unsigned time, sensor_env_t data);
    void sensorTankValueChanged( int index, unsigned time, sensor_tank_t data);

private slots:
    void sensorValueIsChanged(int index, unsigned time, sensor_env_t data);

private:
    void run();
    int  openScanner(int &hciDev);
    void readLoop(int device);
    void waitRetry(int attempt);

    QVector<sensor_t> *m_sensors;
    QStringList        m_macs;      /*by sensor index, read by the thread*/
    std::atomic<bool>  m_stop;
    Heartbeat          m_heartbeat;
    BleStatus          m_status;

};

#endif // BLESENSORS_H
