#ifndef SCREENSAVER_H
#define SCREENSAVER_H

#include <QIcon>
#include <QPixmap>
#include <QWidget>
#include "common.h"
#include "configfile.h"
#include "zonecontrol.h"

class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QVBoxLayout;
class Weather;

/*
 * Screen saver page, shown by MainWindow after a while without touches:
 * clock and date at the top, the outdoor weather under them (icon,
 * temperature, humidity, wind, rain; Weather), the webcam image in the middle (downloaded
 * from webcam_url while shown, every webcam_refresh_s; the last good one
 * stays on a failed download), a summary of alarms and house mode and a
 * flame with the name of each room heating at the bottom (SaverView). A
 * press asks for the main page.
 */
class ScreenSaver : public QWidget
{
    Q_OBJECT
public:
    /* A download taking longer is aborted */
    static const int DOWNLOAD_TIMEOUT_MS = 15000;

    /* weather: nullptr when none */
    ScreenSaver(const QVector<sensor_t> &sensors, ZoneControl *control,
                const screensaver_t &config, const Weather *weather = nullptr,
                QWidget *parent = nullptr);

    /* The image downloaded last, null if none (tests) */
    const QPixmap &webcamImage() const { return m_pixmap; }

public slots:
    /* A new reading of sensor index (BLE or fake thread) */
    void onReading(int index, unsigned time, sensor_env_t data);
    /* MQTT connection: QMqttClient::ClientState */
    void onBrokerState(quint8 state);
    void refresh();

signals:
    void wakeRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void download();
    void downloaded(QNetworkReply *reply);
    void showImage();

    ZoneControl         *m_control;
    screensaver_t        m_config;
    QStringList          m_names;
    QVector<bool>        m_hasZone;
    QVector<bool>        m_noHeating;
    QVector<qint64>      m_lastMs;          /*MonoClock of the last reading, 0 = never*/
    QVector<sensor_env_t> m_last;
    bool                 m_brokerConnected;

    QLabel              *m_clock;
    QLabel              *m_date;
    QLabel              *m_image;
    QWidget             *m_weather;
    QLabel              *m_weatherIcon;
    QLabel              *m_weatherTemp;
    QLabel              *m_weatherDetails;
    QString              m_weatherIconShown;
    const Weather       *m_weatherSource;
    QVBoxLayout         *m_summary;
    QWidget             *m_heating;         /*one chip per room heating, rows of three*/
    QIcon                m_flame;
    QStringList          m_heatingShown;
    QPixmap              m_pixmap;          /*as downloaded*/
    QNetworkAccessManager *m_net;
    QNetworkReply       *m_reply;
    QTimer              *m_clockTimer;
    QTimer              *m_webcamTimer;
    QTimer              *m_abortTimer;
    bool                 m_failed;          /*last download failed (logged once)*/
};

#endif // SCREENSAVER_H
