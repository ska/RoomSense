#include "zonecontrol.h"
#include "logging.h"
#include "monoclock.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

/**
 * @brief ZoneControl::ZoneControl
 * @param zones             AcquaThermoNet zone names by index, empty = no zone
 * @param commandDelayMs    presses collected for this long, then one command
 * @param confirmTimeoutMs  no state_temp after the command: back to the confirmed value
 */
ZoneControl::ZoneControl(const QStringList &zones, int commandDelayMs, int confirmTimeoutMs, QObject *parent) :
    QObject(parent),
    m_zones(zones),
    m_state(zones.size()),
    m_sent(zones.size(), false),
    m_connected(false),
    m_statusOnline(false),
    m_hasMode(false),
    m_mode(Normal),
    m_remainingS(0),
    m_modeAtMs(0),
    m_modePending(false),
    m_pendingMode(Normal)
{
    m_modeTimer = new QTimer(this);
    m_modeTimer->setSingleShot(true);
    m_modeTimer->setInterval(confirmTimeoutMs);
    connect(m_modeTimer, &QTimer::timeout, this, [this] {
        qCWarning(lcMqtt).noquote() << QString("House mode %1 not confirmed by AcquaThermoNet").arg(modeName(m_pendingMode));
        m_modePending = false;
        emit houseModeChanged();
    });

    for(int i = 0; i < zones.size(); i++)
    {
        QTimer *t = new QTimer(this);
        t->setSingleShot(true);
        t->setInterval(commandDelayMs);
        connect(t, &QTimer::timeout, this, [this, i] { commandDue(i); });
        m_commandTimers.append(t);

        t = new QTimer(this);
        t->setSingleShot(true);
        t->setInterval(confirmTimeoutMs);
        connect(t, &QTimer::timeout, this, [this, i] { confirmTimeout(i); });
        m_confirmTimers.append(t);
    }
}

int ZoneControl::indexOf(const QString &zone) const
{
    return zone.isEmpty() ? -1 : m_zones.indexOf(zone);
}

double ZoneControl::normalize(double value)
{
    return qBound(MIN, qRound(value / STEP) * STEP, MAX);
}

bool ZoneControl::canChange(int index) const
{
    return index >= 0 && index < m_zones.size() && !m_zones.at(index).isEmpty()
            && controllerOnline() && m_state.at(index).hasSetPoint
            && m_mode == Normal && !m_modePending;
}

QString ZoneControl::modeName(HouseMode mode)
{
    switch(mode)
    {
    case Window:    return "window";
    case Away:      return "away";
    case Boost:     return "boost";
    default:        return "normal";
    }
}

int ZoneControl::remainingS() const
{
    if(m_mode != Window && m_mode != Boost)
        return 0;
    return qMax<qint64>(0, m_remainingS - (MonoClock::nowMs() - m_modeAtMs) / 1000);
}

bool ZoneControl::canChangeMode() const
{
    return controllerOnline() && m_hasMode;
}

/**
 * @brief ZoneControl::requestMode
 * Sent at once (one press, no collecting); setpoint changes in progress
 * are dropped: the mode applies to every zone
 */
bool ZoneControl::requestMode(HouseMode mode)
{
    if(!canChangeMode())
        return false;
    cancelAllPending();
    m_modePending = true;
    m_pendingMode = mode;
    m_modeTimer->start();
    emit houseModeChanged();
    emit sendMode(modeName(mode));
    return true;
}

void ZoneControl::cancelAllPending()
{
    for(int i = 0; i < m_state.size(); i++)
    {
        if(m_state.at(i).pending)
        {
            cancelPending(i);
            emit zoneChanged(i);
        }
    }
}

void ZoneControl::onModeState(const QByteArray &payload)
{
    const QJsonObject o = QJsonDocument::fromJson(payload).object();
    const QString name = o.value("mode").toString();
    HouseMode mode;
    if(name == "normal")
        mode = Normal;
    else if(name == "window")
        mode = Window;
    else if(name == "away")
        mode = Away;
    else if(name == "boost")
        mode = Boost;
    else
    {
        qCWarning(lcMqtt).noquote() << "Invalid house mode state:" << QString(payload);
        return;
    }

    const bool changed = !m_hasMode || mode != m_mode;
    m_hasMode = true;
    m_mode = mode;
    m_remainingS = qMax(0, o.value("remaining_s").toInt());
    m_modeAtMs = MonoClock::nowMs();
    /* the answer to the request (also when refused: the mode shown is the real one) */
    if(m_modePending)
    {
        m_modePending = false;
        m_modeTimer->stop();
    }
    if(changed && mode != Normal)
        cancelAllPending();
    if(changed)
        qCInfo(lcMqtt).noquote() << "House mode" << name;
    emit houseModeChanged();
    if(changed)
    {
        for(int i = 0; i < m_state.size(); i++)
            emit zoneChanged(i);        /* -/+ enabled or not */
    }
}

/**
 * @brief ZoneControl::step
 * The new value is shown as pending at once; the command leaves after
 * commandDelayMs without other presses
 */
bool ZoneControl::step(int index, int steps)
{
    if(!canChange(index))
        return false;

    setPending(index, shown(index) + steps * STEP);
    return true;
}

double ZoneControl::shown(int index) const
{
    const zone_t &z = m_state.at(index);
    return z.pending ? z.pendingValue : z.setPoint;
}

/**
 * @brief ZoneControl::setPending
 * The value is shown as pending at once; the command leaves after
 * commandDelayMs without other presses
 */
void ZoneControl::setPending(int index, double value)
{
    zone_t &z = m_state[index];
    z.pending = true;
    z.pendingValue = normalize(value);
    m_sent[index] = false;
    m_confirmTimers.at(index)->stop();
    m_commandTimers.at(index)->start();
    emit zoneChanged(index);
}

bool ZoneControl::range(double &low, double &high) const
{
    bool any = false;
    for(int i = 0; i < m_state.size(); i++)
    {
        if(!canChange(i))
            continue;
        const double v = shown(i);
        low = any ? qMin(low, v) : v;
        high = any ? qMax(high, v) : v;
        any = true;
    }
    return any;
}

bool ZoneControl::stepAll(int direction)
{
    double low, high;
    if(direction == 0 || !range(low, high))
        return false;

    double target;
    if(direction < 0)
        target = (low == high) ? low - STEP : low;
    else
        target = (low == high) ? high + STEP : high;

    for(int i = 0; i < m_state.size(); i++)
    {
        /* unchanged zones too: a command for each, as the panel shows them all */
        if(canChange(i))
            setPending(i, target);
    }
    return true;
}

void ZoneControl::commandDue(int index)
{
    if(!canChange(index) || !m_state.at(index).pending)
        return;
    m_sent[index] = true;
    m_confirmTimers.at(index)->start();
    emit sendSetPoint(m_zones.at(index), m_state.at(index).pendingValue);
}

void ZoneControl::confirmTimeout(int index)
{
    qCWarning(lcMqtt).noquote() << QString("Zone %1: setpoint not confirmed by AcquaThermoNet, showing %2 again")
                                   .arg(m_zones.at(index)).arg(m_state.at(index).setPoint);
    cancelPending(index);
    emit zoneChanged(index);
}

void ZoneControl::cancelPending(int index)
{
    m_commandTimers.at(index)->stop();
    m_confirmTimers.at(index)->stop();
    m_sent[index] = false;
    m_state[index].pending = false;
}

void ZoneControl::updateOnline(bool wasOnline)
{
    const bool online = controllerOnline();
    if(online == wasOnline)
        return;
    /* not reachable: changes in progress are dropped, setpoints shown as last known */
    if(!online)
    {
        cancelAllPending();
        if(m_modePending)
        {
            m_modePending = false;
            m_modeTimer->stop();
            emit houseModeChanged();
        }
    }
    emit controllerOnlineChanged(online);
}

void ZoneControl::setConnected(bool connected)
{
    const bool was = controllerOnline();
    m_connected = connected;
    updateOnline(was);
}

void ZoneControl::onStatus(const QByteArray &payload)
{
    const bool was = controllerOnline();
    m_statusOnline = (payload == "online");
    updateOnline(was);
}

void ZoneControl::onStateTemp(const QString &zone, const QByteArray &payload)
{
    const int i = indexOf(zone);
    if(i < 0)
        return;
    bool ok;
    const double v = payload.toDouble(&ok);
    if(!ok)
    {
        qCWarning(lcMqtt).noquote() << QString("Invalid state_temp for zone %1: %2").arg(zone, QString(payload));
        return;
    }
    zone_t &z = m_state[i];
    z.hasSetPoint = true;
    z.setPoint = v;
    /* after the command: the confirmation (the value may differ: clamped,
     * or changed meanwhile from the controller panel or Home Assistant) */
    if(z.pending && m_sent.at(i))
        cancelPending(i);
    emit zoneChanged(i);
}

void ZoneControl::onStateMode(const QString &zone, const QByteArray &payload)
{
    const int i = indexOf(zone);
    if(i < 0)
        return;
    zone_t &z = m_state[i];
    z.hasHeat = true;
    z.heat = (payload == "heat");
    emit zoneChanged(i);
}
