#ifndef MQTT_H
#define MQTT_H

#include "common.h"
#include "zonecontrol.h"
#include <QtMqtt/QMqttClient>

/*
 * Sensor gateway side of the AcquaThermoNet MQTT interface
 * (HA_Hmi_Relay docs/SENSOR_GATEWAY_INTERFACE.md): one reading per zone on
 * RoomSense/<zone>/data, only real and plausible new readings,
 * nothing for a zone whose sensor is no longer heard. One sensor per zone.
 * Panel setpoints (§7): the AcquaThermoNet status, state_temp and
 * state_mode topics feed ZoneControl, its commands go out on set_temp;
 * the house mode (§8) on mode/state and mode/set.
 * Home Assistant discovery only for the sensors outside the heating
 * (heating=false), again when homeassistant/status is online.
 */
class Mqtt : public QObject
{
    Q_OBJECT

public:
    Mqtt(mqtt_brk_t *mbi, QVector<sensor_t> *sensors, QObject *parent = nullptr);

    void setClientState(const QMqttClient::ClientState &newClientState);
    const QVector<quint32> &statsSentData() const;
    /* Readings dropped by the plausibility filter, by sensor */
    const QVector<quint32> &statsFiltered() const { return m_statsFiltered; }

    /* Diagnostic page */
    QMqttClient::ClientState clientState() const { return m_clientState; }
    QString brokerHost() const { return m_client->hostname(); }
    quint16 brokerPort() const { return m_client->port(); }
    QString clientId() const { return m_client->clientId(); }

    /* Exit: offline published, disconnect, no reconnect afterwards; keep
     * processing events until isDisconnected() to flush the socket */
    void shutdown();
    bool isDisconnected() const;

    /* Setpoints of the zones and the panel commands */
    ZoneControl *zoneControl() const { return m_control; }

    /* Home Assistant discovery of a sensor outside the heating (topic, payload) */
    static QVector<QPair<QString, QByteArray>> discovery(const QString &uniqueId, const sensor_t &sensor);

public slots:
    void sensorValueIsChanged(int index, unsigned time, sensor_env_t data);

signals:
    void clientStateChanged( quint8 );
    void messageSent(qint32);

private slots:
    void connectToBroker();
    void messageSentSlot(qint32 id);
    void stateChangedSlot();
    void messageReceivedSlot(const QByteArray &message, const QMqttTopicName &topic);
    void publishSetPoint(const QString &zone, double value);
    void publishMode(const QString &mode);
    void publishChronoOn(const QString &zone, bool on);
    void publishChronoProgram(const QString &zone, const QByteArray &payload);

private:
    /* Last accepted reading of a sensor */
    typedef struct {
        bool     valid = false;
        qint64   time = 0;          /*monotonic s*/
        unsigned ldt = 0;           /*epoch s of the reading*/
        qint16   temp = 0;          /*0.1 degC*/
        quint8   hum = 0;
        quint8   batt = 0;
        quint16  battmv = 0;
        /*reading waiting for confirmation: a jump, or the first one*/
        bool     pending = false;
        qint64   pendingTime = 0;
        qint16   pendingTemp = 0;
    } reading_t;

    /* Last message published for a sensor's zone */
    typedef struct {
        bool     valid = false;
        qint64   time = 0;          /*monotonic s*/
        int      temp = 0;          /*0.1 degC*/
    } zone_pub_t;

    /* Plausibility of a reading: Confirming (first one, or after a silence:
     * nothing to compare with) is not counted as filtered */
    enum Verdict { Accepted, Confirming, Filtered };

    void scheduleReconnect();
    Verdict acceptReading(int index, const sensor_env_t &data);
    void publishZone(int index);
    void publishDiscovery();
    qint64 monoSecs() const;

    mqtt_brk_t          *m_mbi;
    QMqttClient         *m_client;
    QString             m_uniqueId;     /*[MQTT] unique_id, else the host name*/
    QTimer              *m_reconnectTimer;
    QTimer              *m_connectTimer;
    QVector<sensor_t>   *m_sensors;
    quint16             m_connectingTry;
    bool                m_shuttingDown;

    QMqttClient::ClientState m_clientState;
    QVector<quint32> m_statsSentData;
    QVector<quint32> m_statsFiltered;
    ZoneControl     *m_control;

    QVector<reading_t>          m_readings;
    QVector<zone_pub_t>         m_zones;    /*by sensor: one sensor per zone*/
};

#endif // MQTT_H
