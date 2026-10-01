#ifndef DIAGPAGE_H
#define DIAGPAGE_H

#include <QWidget>
#include "common.h"

class BleStatus;
class Mqtt;
class QLabel;
class QFrame;
class QPlainTextEdit;
class QTabWidget;
class WatchDog;
class Weather;

/*
 * Diagnostic page, opened by a long press on the clock of the main page,
 * in three tabs: System (broker, controller, BLE adapter, watchdog, log
 * file, weather), Sensors (one card each: RSSI, last reading, messages sent,
 * readings filtered) and Log (the last messages). Texts and levels come
 * from DiagView, colors from the stylesheet ("level" property).
 * Refreshed every second while visible.
 */
class DiagPage : public QWidget
{
    Q_OBJECT
public:
    /* ble, watchdog, weather: nullptr when there is none */
    DiagPage(const QVector<sensor_t> *sensors, Mqtt *mqtt, const BleStatus *ble,
             const WatchDog *watchdog, const Weather *weather = nullptr, QWidget *parent = nullptr);

public slots:
    /* A new reading of sensor index (BLE or fake thread) */
    void onReading(int index, unsigned time, sensor_env_t data);
    void refresh();

signals:
    void backRequested();

protected:
    void showEvent(QShowEvent *event) override;

private:
    typedef struct {
        QFrame *card;
        QLabel *rssi;
        QLabel *read;
        QLabel *sent;
        QLabel *filtered;
    } sensor_card_t;

    const QVector<sensor_t> *m_sensors;
    Mqtt                *m_mqtt;
    const BleStatus     *m_ble;
    const WatchDog      *m_watchdog;
    const Weather       *m_weather;
    qint64               m_startMs;
    QVector<qint64>      m_lastMs;          /*MonoClock of the last reading, 0 = never*/
    QVector<sensor_env_t> m_last;
    quint64              m_logShown;

    QVector<QLabel *>    m_dots;
    QVector<QLabel *>    m_values;
    QVector<sensor_card_t> m_cards;
    QTabWidget          *m_tabs;
    QPlainTextEdit      *m_log;
};

#endif // DIAGPAGE_H
