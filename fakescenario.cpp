#include "fakescenario.h"
#include <QRegularExpression>
#include <QtMath>

/**
 * @brief FakeScenario::FakeScenario
 * @param sensors   number of configured sensors
 * @param spec      scenario (see fakescenario.h), empty: normal drift only
 */
FakeScenario::FakeScenario(int sensors, const QString &spec) :
    m_sensors(sensors),
    m_cold(sensors, false),
    m_noBatt(sensors, false)
{
    if(!parse(spec))
    {
        m_events.clear();
        m_modes.clear();
        m_cold.fill(false);
        m_noBatt.fill(false);
    }
}

bool FakeScenario::parse(const QString &spec)
{
    static const QRegularExpression re("^(lost|back|spike|out|step|press|cold|nobatt)=(\\d+)(?:@(\\d+))?(?::([+-]?\\d+))?$");
    static const QRegularExpression modeRe("^mode=(normal|window|away|boost)@(\\d+)$");

    for(const QString &raw : spec.split(';'))
    {
        const QString item = raw.trimmed();
        if(item.isEmpty())
            continue;
        const QRegularExpressionMatch mm = modeRe.match(item);
        if(mm.hasMatch())
        {
            m_modes.append(qMakePair(mm.captured(2).toLongLong(), mm.captured(1)));
            continue;
        }
        const QRegularExpressionMatch m = re.match(item);
        const int sensor = m.hasMatch() ? m.captured(2).toInt() : -1;
        if(!m.hasMatch() || sensor >= m_sensors)
        {
            m_error = QString("invalid item '%1'").arg(item);
            return false;
        }

        const QString name = m.captured(1);
        const bool hasAt = !m.captured(3).isEmpty();
        const bool hasValue = !m.captured(4).isEmpty();
        if(name == "cold" || name == "nobatt")
        {
            if(hasAt || hasValue)
            {
                m_error = QString("'%1' takes only the sensor").arg(item);
                return false;
            }
            (name == "cold" ? m_cold : m_noBatt)[sensor] = true;
            continue;
        }

        event_t e;
        e.sensor = sensor;
        e.at = m.captured(3).toLongLong();
        e.value = m.captured(4).toInt();
        const bool needsValue = (name == "spike" || name == "out" || name == "step" || name == "press");
        if(!hasAt || hasValue != needsValue)
        {
            m_error = QString("'%1': expected %2").arg(item, needsValue ? "S@T:V" : "S@T");
            return false;
        }
        if(name == "lost")       e.type = event_t::Lost;
        else if(name == "back")  e.type = event_t::Back;
        else if(name == "spike") e.type = event_t::Spike;
        else if(name == "out")   e.type = event_t::Out;
        else if(name == "press") e.type = event_t::Press;
        else                     e.type = event_t::Step;
        m_events.append(e);
    }
    return true;
}

/* Silent if the latest lost/back event of the sensor before t is a lost */
bool FakeScenario::silent(int sensor, qint64 t) const
{
    bool lost = false;
    qint64 latest = -1;
    for(const event_t &e : m_events)
    {
        if(e.sensor != sensor || e.at > t || e.at < latest)
            continue;
        if(e.type == event_t::Lost || e.type == event_t::Back)
        {
            latest = e.at;
            lost = (e.type == event_t::Lost);
        }
    }
    return lost;
}

bool FakeScenario::reading(int sensor, qint64 t, sensor_env_t &env) const
{
    if(sensor < 0 || sensor >= m_sensors || silent(sensor, t))
        return false;

    /* slow drift: +-1.5 degC over 10 minutes around the base of the sensor */
    const int base = m_cold.at(sensor) ? -50 : 185 + 10 * (sensor % 5);
    int temp = base + qRound(15 * qSin(2 * M_PI * (t + 60 * sensor) / 600.0));

    for(const event_t &e : m_events)
    {
        if(e.sensor != sensor || t < e.at)
            continue;
        const bool once = t < e.at + PERIOD_S;          /* the reading near T only */
        if(e.type == event_t::Step)
            temp += e.value;
        else if(e.type == event_t::Spike && once)
            temp += e.value;
        else if(e.type == event_t::Out && once)
            temp = e.value;
    }

    memset(&env, 0, sizeof(env));
    env.temp = qint16(temp);
    env.hum  = quint8(50 + qRound(10 * qCos(2 * M_PI * t / 900.0)));
    if(!m_noBatt.at(sensor))
    {
        env.batt   = quint8(qMax<qint64>(1, 90 - t / 3600));  /* -1 % per hour */
        env.battmv = quint16(2600 + 5 * env.batt);
    }
    env.pc   = quint8((t / PERIOD_S) & 0xFF);
    env.rssi = qint8(-60 - 5 * (sensor % 4));
    return true;
}

QVector<QPair<int, int>> FakeScenario::presses(qint64 from, qint64 to) const
{
    QVector<QPair<int, int>> out;
    for(const event_t &e : m_events)
        if(e.type == event_t::Press && e.at >= from && e.at < to)
            out.append(qMakePair(e.sensor, e.value));
    return out;
}

QStringList FakeScenario::modes(qint64 from, qint64 to) const
{
    QStringList out;
    for(const QPair<qint64, QString> &m : m_modes)
        if(m.first >= from && m.first < to)
            out.append(m.second);
    return out;
}
