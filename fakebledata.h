#ifndef FAKEBLEDATA_H
#define FAKEBLEDATA_H

#include "common.h"
#include "blestatus.h"
#include "fakescenario.h"
#include <atomic>

Q_DECLARE_METATYPE(sensor_t)
Q_DECLARE_METATYPE(sensor_env_t)

/*
 * Desktop build: generated readings instead of the BLE scan, one per
 * sensor every FakeScenario::PERIOD_S seconds, with the events of the
 * scenario given in the RS_FAKE environment variable (fakescenario.h).
 */
class FakeBleData : public QThread
{
    Q_OBJECT
public:
    explicit FakeBleData(QVector<sensor_t> *sensors, const QString &scenario = QString());
    void stop();
    /* Simulated adapter: last generated reading (diagnostic page) */
    const BleStatus &status() const { return m_status; }

signals:
    void sensorEnvValueChanged( int index, unsigned time, sensor_env_t data);
    /* press=S@T:N of the scenario: steps on the panel for sensor index */
    void panelPress( int index, int steps );
    /* mode=M@T of the scenario: house mode chosen on the panel */
    void panelMode( const QString &mode );

private slots:
    void sensorValueIsChanged(int index, unsigned time, sensor_env_t data);

private:
    void run();

    QVector<sensor_t>  *m_sensors;
    FakeScenario        m_scenario;
    std::atomic<bool>   m_stop;
    BleStatus           m_status;
};

#endif // FAKEBLEDATA_H
