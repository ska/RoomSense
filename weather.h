#ifndef WEATHER_H
#define WEATHER_H

#include <QDateTime>
#include <QObject>
#include "configfile.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

typedef struct {
    QString location;
    double  temp    = 0;        /* °C */
    int     hum     = 0;        /* % */
    double  ws      = 0;        /* wind m/s */
    double  rain    = 0;        /* mm, next hour */
    int     press   = 0;        /* hPa */
    QString symbol;             /* met.no symbol_code ("partlycloudy_day"), empty: unknown */
} weather_t;

/*
 * Outdoor weather for the screen saver, from api.met.no (locationforecast
 * compact) or wttr.in, as AcquaThermoNet (Weather of HA_Hmi_Relay). For
 * met.no the terms of service are followed: identifying User-Agent, no
 * request before Expires, If-Modified-Since. Both are HTTPS: OpenSSL is
 * needed (on the panel; the desktop Qt 5.13 does not load OpenSSL 3).
 */
class Weather : public QObject
{
    Q_OBJECT
public:
    explicit Weather(const weather_cfg_t &config, QObject *parent = nullptr);
    void startPoll();

    bool enabled() const { return m_config.enabled(); }
    /* "met.no", "wttr.in" */
    QString source() const;
    /* The last good data and when it arrived (MonoClock, 0 = never) */
    const weather_t &last() const { return m_last; }
    qint64 lastOkMs() const { return m_lastOkMs; }
    /* The error of the last request, empty after a good one */
    QString lastError() const { return m_lastError; }

    /* wttr.in answer with format "%l:+%t+%h+%w+%p+%P" and &M (wind in m/s) */
    static bool parseWttr(const QString &answer, weather_t &out);
    /* api.met.no locationforecast/2.0/compact JSON, first time step */
    static bool parseMetNo(const QByteArray &json, const QString &name, weather_t &out);
    /* HTTP date "Tue, 29 Sep 2026 14:44:19 GMT" (RFC 7231), invalid if not parsed */
    static QDateTime parseHttpDate(const QByteArray &value);

signals:
    void newWeather(const weather_t &data);

private slots:
    void request();
    void replyFinished(QNetworkReply *reply);

private:
    QByteArray userAgent() const;
    void failed(const QString &error);

    QNetworkAccessManager   *m_manager;
    QTimer                  *m_timer;
    weather_cfg_t            m_config;
    weather_t                m_last;
    qint64                   m_lastOkMs;
    QString                  m_lastError;

    /* met.no cache control */
    QByteArray               m_lastModified;
    QDateTime                m_expires;
};

#endif // WEATHER_H
