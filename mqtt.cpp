#include "mqtt.h"
#include "monoclock.h"
#include <QJsonDocument>
#include <QJsonObject>

/* AcquaThermoNet interface: HA_Hmi_Relay docs/SENSOR_GATEWAY_INTERFACE.md */
#define TOPIC_ZONE          "RoomSense/apartment/%1/data"
#define TOPIC_STATUS        "RoomSense/status"

/* AcquaThermoNet topics for the panel setpoints (interface §7) */
#define CTRL_BASE           "AcquaThermoNet"
#define CTRL_STATUS         CTRL_BASE "/status"
#define CTRL_STATE_TEMP     "state_temp"
#define CTRL_STATE_MODE     "state_mode"
#define CTRL_SET_TEMP       CTRL_BASE "/%1/set_temp"
/* House mode (interface §8) */
#define CTRL_MODE_STATE     CTRL_BASE "/mode/state"
#define CTRL_MODE_SET       CTRL_BASE "/mode/set"

/* Plausibility (0.1 degC): the controller does not range-check */
#define TEMP_MIN_DC         (-200)  /*-20.0 degC*/
#define TEMP_MAX_DC         500     /* 50.0 degC*/
#define TEMP_JUMP_DC        30      /*jump from the previous reading to be confirmed*/
#define TEMP_CONFIRM_DC     5       /*a later reading this close confirms the jump*/
#define JUMP_WINDOW_S       300     /*jumps checked against readings younger than this*/
#define CONFIRM_MIN_S       10      /*confirmation from a later measurement, not the same advertisement*/

/* Rate, per zone */
#define CHANGE_DC           1       /*0.1 degC change published before the interval*/
#define CHANGE_MIN_S        10      /*at most one message every 10 s*/

/* Connection */
#define CONNECT_TIMEOUT_MS  30000   /*no CONNACK: give up and retry*/

Mqtt::Mqtt(mqtt_brk_t *mbi, QVector<sensor_t> *sensors, QObject *parent) :
    QObject(parent),
    m_mbi(mbi),
    m_sensors(sensors)
{
    m_connectingTry = 0;
    m_shuttingDown = false;
    m_clientState = QMqttClient::Disconnected;
    m_statsSentData.fill(0, m_sensors->length());
    m_statsFiltered.fill(0, m_sensors->length());
    m_readings.fill(reading_t(), m_sensors->length());
    m_zones.fill(zone_pub_t(), m_sensors->length());

    QStringList zones;
    for(const sensor_t &s : *m_sensors)
        zones.append( QString::fromLatin1(s.zone) );
    m_control = new ZoneControl(zones, 1000, 5000, this);
    connect(m_control, &ZoneControl::sendSetPoint, this, &Mqtt::publishSetPoint);
    connect(m_control, &ZoneControl::sendMode, this, &Mqtt::publishMode);

    qCDebug(lcMqtt) << "Mqtt::Mqtt " <<  m_mbi->address << " " << m_mbi->port;
    m_client = new QMqttClient(this);
    m_client->setHostname( m_mbi->address );
    m_client->setPort( m_mbi->port );
    if( m_mbi->uname[0] != '\0' && m_mbi->password[0] != '\0' )
    {
        m_client->setUsername( m_mbi->uname );
        m_client->setPassword( m_mbi->password );
    }
    /* Unique and stable, must not collide with AcquaThermoNet-... */
    const QString uniqueId = m_mbi->uniqueId[0] != '\0' ? QString::fromUtf8(m_mbi->uniqueId)
                                                         : QHostInfo::localHostName();
    m_client->setClientId( QString(SW_NAME "-%1").arg(uniqueId) );
    qCInfo(lcMqtt) << "MQTT client id: " << m_client->clientId();
    m_client->setKeepAlive(30);

    /* Gateway availability */
    m_client->setWillTopic( TOPIC_STATUS );
    m_client->setWillMessage( "offline" );
    m_client->setWillQoS(1);
    m_client->setWillRetain(true);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &Mqtt::connectToBroker);

    /* broker that accepts TCP but never answers CONNECT */
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, m_client, &QMqttClient::disconnectFromHost);
    /*
     * SIGNALS
     */
    connect(m_client, SIGNAL(messageSent(qint32)), this, SLOT(messageSentSlot(qint32)));
    connect(m_client, SIGNAL(messageSent(qint32)), this, SIGNAL(messageSent(qint32)));

    connect(m_client, &QMqttClient::stateChanged, this, &Mqtt::stateChangedSlot );
    connect(m_client, &QMqttClient::messageReceived, this, &Mqtt::messageReceivedSlot );

    connectToBroker();
}

/**
 * @brief Mqtt::shutdown
 * Clean disconnect does not trigger the Will: publish offline ourselves.
 * No reconnect afterwards; keep processing events to flush the socket.
 */
void Mqtt::shutdown()
{
    m_shuttingDown = true;
    m_control->setConnected(false);
    m_reconnectTimer->stop();
    m_connectTimer->stop();
    if( m_client->state() == QMqttClient::Connected )
        m_client->publish( QMqttTopicName(TOPIC_STATUS), "offline", 1, true );
    m_client->disconnectFromHost();
}

bool Mqtt::isDisconnected() const
{
    return m_client->state() == QMqttClient::Disconnected;
}

/**
 * @brief Mqtt::monoSecs
 * @return monotonic seconds (MonoClock: advanced by the tests)
 */
qint64 Mqtt::monoSecs() const
{
    return MonoClock::nowMs() / 1000;
}

/**
 * @brief Mqtt::connectToBroker
 */
void Mqtt::connectToBroker()
{
    m_client->connectToHost();
}

/**
 * @brief Mqtt::scheduleReconnect
 * Backoff 1, 2, 5, 10, then 30s, as AcquaThermoNet
 */
void Mqtt::scheduleReconnect()
{
    static const int backoff[] = { 1000, 2000, 5000, 10000, 30000 };
    static const int backoffLen = sizeof(backoff) / sizeof(backoff[0]);

    if( m_shuttingDown || m_reconnectTimer->isActive() )
        return;

    const int delay = backoff[qMin<int>(m_connectingTry, backoffLen - 1)];
    qCDebug(lcMqtt) << "MQTT disconnected, retry in " << delay << "ms";
    m_connectingTry++;
    m_reconnectTimer->start(delay);
}

/**
 * @brief Mqtt::sensorValueIsChanged
 * New BLE reading: filtered, then published for its zone when due
 * @param index
 * @param time
 * @param data
 */
void Mqtt::sensorValueIsChanged(int index, unsigned time, sensor_env_t data)
{
    (void) time;

    if( index < 0 || index >= m_readings.length() )
        return;
    if( m_sensors->at(index).zone[0] == '\0' )
        return;
    if( !acceptReading(index, data) )
    {
        m_statsFiltered[index]++;
        return;
    }

    publishZone( index );
}

/**
 * @brief Mqtt::acceptReading
 * Drops implausible readings: out of range, or a jump not confirmed by a
 * later measurement. Dropping is always safe, publishing a wrong one is not.
 * @return true if stored as the sensor's last reading
 */
bool Mqtt::acceptReading(int index, const sensor_env_t &data)
{
    reading_t &r = m_readings[index];
    const qint64 now = monoSecs();
    const QString name = QString::fromUtf8(m_sensors->at(index).name);

    if( data.temp < TEMP_MIN_DC || data.temp > TEMP_MAX_DC )
    {
        qCInfo(lcMqtt).noquote() << QString("Sensor %1: temperature %2 out of range, dropped").arg(name).arg(data.temp / 10.0);
        return false;
    }

    if( r.valid && (now - r.time) <= JUMP_WINDOW_S && qAbs(data.temp - r.temp) > TEMP_JUMP_DC )
    {
        const bool confirmed = r.pending
                && (now - r.pendingTime) >= CONFIRM_MIN_S
                && qAbs(data.temp - r.pendingTemp) <= TEMP_CONFIRM_DC;
        if( !confirmed )
        {
            if( !r.pending || qAbs(data.temp - r.pendingTemp) > TEMP_CONFIRM_DC )
            {
                qCInfo(lcMqtt).noquote() << QString("Sensor %1: jump %2 -> %3, waiting for confirmation")
                                            .arg(name).arg(r.temp / 10.0).arg(data.temp / 10.0);
                r.pending = true;
                r.pendingTime = now;
                r.pendingTemp = data.temp;
            }
            return false;
        }
    }

    r.pending = false;
    r.valid  = true;
    r.time   = now;
    r.ldt    = data.ldt;
    r.temp   = data.temp;
    r.hum    = data.hum;
    r.batt   = data.batt;
    r.battmv = data.battmv;
    return true;
}

/**
 * @brief Mqtt::publishZone
 * The last accepted reading of the sensor, on its zone topic: every
 * publishInterval, or earlier on a change of 0.1 degC.
 * Not connected: nothing is buffered, the next reading is sent instead.
 * @param index sensor, the only one of its zone
 */
void Mqtt::publishZone(int index)
{
    if( m_clientState != QMqttClient::Connected )
        return;

    const qint64 now = monoSecs();
    const reading_t &r = m_readings.at(index);
    const QString zone = QString::fromLatin1(m_sensors->at(index).zone);
    zone_pub_t &z = m_zones[index];
    const qint64 since = now - z.time;
    const bool due = !z.valid
            || since >= m_mbi->publishInterval
            || ( qAbs(r.temp - z.temp) >= CHANGE_DC && since >= CHANGE_MIN_S );
    if( !due )
        return;

    QJsonObject payload;
    payload["temperature"] = r.temp / 10.0;
    if( r.hum > 0 && r.hum <= 100 )
        payload["humidity"] = r.hum;
    /* 0 = unknown: omitted */
    if( r.batt > 0 && r.batt <= 100 )
        payload["battery"] = r.batt;
    if( r.battmv > 0 )
        payload["battmv"] = r.battmv;
    payload["data_time"] = qint64(r.ldt);
    payload["mac"] = QString::fromLatin1( m_sensors->at(index).smac );

    /* retain false: a retained reading would count as fresh at the controller restart */
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const qint32 id = m_client->publish( QMqttTopicName(QString(TOPIC_ZONE).arg(zone)), json, 1, false );
    if( id < 0 )
    {
        qCWarning(lcMqtt).noquote() << QString("Zone %1: publish failed").arg(zone);
        return;
    }
    qCDebug(lcMqtt).noquote() << QString("Zone %1: ").arg(zone).append(json);

    z.valid = true;
    z.time  = now;
    z.temp  = r.temp;

    /* last sent time and counter updated when the broker acknowledges */
    sensor_t tmp = m_sensors->at(index);
    tmp.mqttPubId = id;
    m_sensors->replace(index, tmp);
}

void Mqtt::messageSentSlot(qint32 id)
{
    for(int i=0; i<m_sensors->length(); i++)
    {
        if( m_sensors->at(i).mqttPubId == id)
        {
            sensor_t tmp;
            uint64_t tm = (unsigned)time(NULL);
            tmp = m_sensors->at(i);
            tmp.ldst = tm;
            tmp.mqttPubId = -1;
            m_sensors->replace(i, tmp);
            m_statsSentData.replace( i, m_statsSentData.at(i)+1 );
            break;
        }
    }
}

void Mqtt::stateChangedSlot()
{
    setClientState( m_client->state() );
    qCDebug(lcMqtt) << "stateChangedSlot: " << m_clientState;

    if( m_clientState == QMqttClient::Disconnected )
    {
        m_connectTimer->stop();
        m_control->setConnected(false);
        scheduleReconnect();
    }
    else if( m_clientState == QMqttClient::Connecting )
    {
        m_connectTimer->start(CONNECT_TIMEOUT_MS);
    }
    else if( m_clientState == QMqttClient::Connected )
    {
        qCInfo(lcMqtt) << "MQTT client connected to broker";
        m_connectTimer->stop();
        m_reconnectTimer->stop();
        m_connectingTry = 0;
        m_client->publish( QMqttTopicName(TOPIC_STATUS), "online", 1, true );

        /* clean session: subscribe again at every connection; the retained
         * status and states arrive at once */
        const QStringList filters = { CTRL_STATUS, CTRL_BASE "/+/" CTRL_STATE_TEMP, CTRL_BASE "/+/" CTRL_STATE_MODE,
                                      CTRL_MODE_STATE };
        for( const QString &f : filters )
        {
            if( !m_client->subscribe(QMqttTopicFilter(f)) )
                qCWarning(lcMqtt) << "Subscription failed:" << f;
        }
        m_control->setConnected(true);
    }
}

/**
 * @brief Mqtt::messageReceivedSlot
 * AcquaThermoNet status and zone states, for ZoneControl
 */
void Mqtt::messageReceivedSlot(const QByteArray &message, const QMqttTopicName &topic)
{
    const QString name = topic.name();
    if( name == CTRL_STATUS )
    {
        m_control->onStatus(message);
        return;
    }
    if( name == CTRL_MODE_STATE )
    {
        m_control->onModeState(message);
        return;
    }
    const QStringList levels = name.split('/');
    if( levels.size() != 3 || levels.at(0) != CTRL_BASE )
        return;
    if( levels.at(2) == CTRL_STATE_TEMP )
        m_control->onStateTemp(levels.at(1), message);
    else if( levels.at(2) == CTRL_STATE_MODE )
        m_control->onStateMode(levels.at(1), message);
}

/**
 * @brief Mqtt::publishSetPoint
 * Setpoint command: plain number, QoS 1, retain false (a retained command
 * would be applied again at every AcquaThermoNet restart)
 */
void Mqtt::publishSetPoint(const QString &zone, double value)
{
    if( m_clientState != QMqttClient::Connected )
        return;
    const QByteArray payload = QByteArray::number(value);
    qCInfo(lcMqtt).noquote() << QString("Zone %1: setpoint %2 sent to AcquaThermoNet").arg(zone, QString(payload));
    if( m_client->publish( QMqttTopicName(QString(CTRL_SET_TEMP).arg(zone)), payload, 1, false ) < 0 )
        qCWarning(lcMqtt).noquote() << QString("Zone %1: setpoint publish failed").arg(zone);
}

/**
 * @brief Mqtt::publishMode
 * House mode command: QoS 1, retain false, as the setpoints
 */
void Mqtt::publishMode(const QString &mode)
{
    if( m_clientState != QMqttClient::Connected )
        return;
    qCInfo(lcMqtt).noquote() << QString("House mode %1 sent to AcquaThermoNet").arg(mode);
    if( m_client->publish( QMqttTopicName(CTRL_MODE_SET), mode.toUtf8(), 1, false ) < 0 )
        qCWarning(lcMqtt).noquote() << QString("House mode %1: publish failed").arg(mode);
}

const QVector<quint32> &Mqtt::statsSentData() const
{
    return m_statsSentData;
}

void Mqtt::setClientState(const QMqttClient::ClientState &newClientState)
{
    if( newClientState != m_clientState)
    {
        m_clientState = newClientState;
        emit clientStateChanged( (quint8)newClientState );
    }
}
