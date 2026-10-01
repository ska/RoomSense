#include "fakebledata.h"
#include "monoclock.h"

FakeBleData::FakeBleData(QVector<sensor_t> *sensors, const QString &scenario) :
    m_sensors(sensors),
    m_scenario(sensors->length(), scenario)
{
    qRegisterMetaType<sensor_t>();
    qRegisterMetaType<sensor_env_t>();

    m_stop = false;
    m_status.set(BleStatus::Simulated);
    if( !m_scenario.isValid() )
        qCWarning(lcBle).noquote() << "Fake data scenario" << scenario << ":" << m_scenario.error() << ", ignored";
    else if( !scenario.isEmpty() )
        qCInfo(lcBle).noquote() << "Fake data scenario:" << scenario;

    connect( this, SIGNAL( sensorEnvValueChanged(int, unsigned, sensor_env_t)), this, SLOT(sensorValueIsChanged(int, unsigned, sensor_env_t)) );
}

void FakeBleData::run()
{
    const qint64 startMs = MonoClock::nowMs();
    qint64 next = 0;
    qint64 pressedUpTo = 0;

    while ( !m_stop )
    {
        const qint64 t = (MonoClock::nowMs() - startMs) / 1000;
        if ( t >= next )
        {
            for ( int i = 0; i < m_sensors->length(); i++ )
            {
                sensor_env_t env;
                if ( !m_scenario.reading(i, t, env) )
                    continue;
                env.ldt = (unsigned)time(NULL);
                m_status.advertisement();
                emit sensorEnvValueChanged( i, env.ldt, env );
            }
            next = t + FakeScenario::PERIOD_S;
        }
        for ( const QPair<int, int> &p : m_scenario.presses(pressedUpTo, t + 1) )
            emit panelPress( p.first, p.second );
        for ( const QString &m : m_scenario.modes(pressedUpTo, t + 1) )
            emit panelMode( m );
        pressedUpTo = t + 1;
        msleep(100);
    }
}

void FakeBleData::stop()
{
    m_stop = true;
}

void FakeBleData::sensorValueIsChanged( int index, unsigned time, sensor_env_t data)
{
    (void) time;
    sensor_t tmp;
    tmp = m_sensors->at(index);
    tmp.se = data;
    m_sensors->replace(index, tmp);
}
