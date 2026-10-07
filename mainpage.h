#ifndef MAINPAGE_H
#define MAINPAGE_H

#include <QWidget>
#include "common.h"
#include "zonecontrol.h"

class ModePopup;
class QLabel;
class QToolButton;
class ZoneCard;

/*
 * Main page of the panel: clock, AcquaThermoNet and network state, one
 * ZoneCard per sensor with its reading and the setpoint of its zone
 * (changed through ZoneControl, owned by AcquaThermoNet), the house mode
 * button in the header (opens ModePopup). A long press on the clock asks for
 * the diagnostic page.
 */
class MainPage : public QWidget
{
    Q_OBJECT
public:
    MainPage(const QVector<sensor_t> &sensors, ZoneControl *control, QWidget *parent = nullptr);

    ZoneCard *card(int index) const { return m_cards.at(index); }
    /* Close the house popup (the screen saver comes) */
    void closePopup();

public slots:
    /* A new reading of sensor index (BLE or fake thread) */
    void onReading(int index, unsigned time, sensor_env_t data);
    /* MQTT connection: QMqttClient::ClientState */
    void onBrokerState(quint8 state);
    /* Clock and ages, every second */
    void refresh();

signals:
    void diagnosticsRequested();
    /* a tap on the chrono line of a zone with a chrono program */
    void chronoRequested(int index);

private:
    void refreshCard(int index);
    void refreshHeader();
    void refreshModeButton();

    ZoneControl         *m_control;
    QVector<bool>        m_hasZone;
    QVector<ZoneCard *>  m_cards;
    QVector<qint64>      m_lastMs;         /*MonoClock of the last reading, 0 = never*/
    QVector<sensor_env_t> m_last;
    bool                 m_brokerConnected;

    QLabel  *m_clock;
    QLabel  *m_date;
    QLabel  *m_controllerDot;
    QLabel  *m_controllerText;
    QLabel  *m_networkDot;
    QLabel  *m_networkText;
    QLabel  *m_banner;
    QToolButton *m_modeButton;
    ModePopup   *m_modePopup;
};

#endif // MAINPAGE_H
