#include "weather.h"
#include "monoclock.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

Weather::Weather(const weather_cfg_t &config, QObject *parent) :
    QObject(parent),
    m_config(config),
    m_lastOkMs(0)
{
    m_manager = new QNetworkAccessManager(this);
    connect(m_manager, &QNetworkAccessManager::finished, this, &Weather::replyFinished);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &Weather::request);
}

QString Weather::source() const
{
    return m_config.provider == weather_cfg_t::MetNo ? "met.no" : "wttr.in";
}

/**
 * @brief Weather::startPoll
 * Polls all the time (every poll_s), not only while the screen saver is
 * shown: the data is there when it comes
 */
void Weather::startPoll()
{
    if(!m_config.enabled())
    {
        qCInfo(lcWeather) << "Weather disabled";
        return;
    }
    qCInfo(lcWeather).noquote() << "Weather from" << source() << "every" << m_config.pollS << "s";
    if(m_config.provider == weather_cfg_t::MetNo && m_config.contact.isEmpty())
        qCWarning(lcWeather) << "met.no asks for a contact in the User-Agent: set WEATHER/contact";

    request();
    m_timer->start(m_config.pollS * 1000);
}

/**
 * @brief Weather::userAgent
 * met.no: application name and version, plus a contact if configured
 */
QByteArray Weather::userAgent() const
{
    QByteArray ua = SW_NAME "/" SW_VER;
    if(!m_config.contact.isEmpty())
        ua += " " + m_config.contact.toUtf8();
    return ua;
}

void Weather::request()
{
    QNetworkRequest req;
    req.setRawHeader("User-Agent", userAgent());

    if(m_config.provider == weather_cfg_t::MetNo)
    {
        /* Terms of service: no new request before the data expires */
        if(m_expires.isValid() && QDateTime::currentDateTimeUtc() < m_expires)
        {
            qCDebug(lcWeather) << "met.no data valid until" << m_expires.toString(Qt::ISODate) << ", no request";
            return;
        }

        QUrl url("https://api.met.no/weatherapi/locationforecast/2.0/compact");
        QUrlQuery q;
        /* max 4 decimals, as asked by met.no */
        q.addQueryItem("lat", QString::number(m_config.lat, 'f', 4));
        q.addQueryItem("lon", QString::number(m_config.lon, 'f', 4));
        q.addQueryItem("altitude", QString::number(m_config.altitude));
        url.setQuery(q);
        req.setUrl(url);
        if(!m_lastModified.isEmpty())
            req.setRawHeader("If-Modified-Since", m_lastModified);
    }
    else
    {
        req.setUrl(QUrl("https://wttr.in/" + m_config.location + "?format=\"%l:+%t+%h+%w+%p+%P\"&M"));
    }
    m_manager->get(req);
}

/**
 * @brief Weather::parseWttr
 * e.g. "home: +12°C 70% ↓3m/s 0.0mm 1015hPa" (with quotes); no symbol
 * @return true if all the fields were found
 */
bool Weather::parseWttr(const QString &answer, weather_t &out)
{
    static const QRegularExpression re(
        R"re(^"(\w+):\s*([+\-]?\d+)\WC\W*(\d+)\W*([\d.]+)m/s\W*([\d.]+)mm\W*(\d+)hPa)re");

    const QRegularExpressionMatch m = re.match(answer);
    if(!m.hasMatch())
        return false;

    out.location = m.captured(1);
    out.temp     = m.captured(2).toInt();
    out.hum      = m.captured(3).toInt();
    out.ws       = m.captured(4).toDouble();
    out.rain     = m.captured(5).toDouble();
    out.press    = m.captured(6).toInt();
    out.symbol.clear();
    return true;
}

/**
 * @brief Weather::parseMetNo
 * properties.timeseries[0]: data.instant.details (air_temperature,
 * relative_humidity, air_pressure_at_sea_level, wind_speed),
 * data.next_1_hours (else next_6_hours): summary.symbol_code and
 * details.precipitation_amount (optional)
 * @return true if temperature, humidity and pressure were found
 */
bool Weather::parseMetNo(const QByteArray &json, const QString &name, weather_t &out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonArray series = doc.object()["properties"].toObject()["timeseries"].toArray();
    if(series.isEmpty())
        return false;

    const QJsonObject data    = series.first().toObject()["data"].toObject();
    const QJsonObject details = data["instant"].toObject()["details"].toObject();

    const QJsonValue temp  = details["air_temperature"];
    const QJsonValue hum   = details["relative_humidity"];
    const QJsonValue press = details["air_pressure_at_sea_level"];
    if(!temp.isDouble() || !hum.isDouble() || !press.isDouble())
        return false;

    QJsonObject next = data["next_1_hours"].toObject();
    if(next.isEmpty())
        next = data["next_6_hours"].toObject();

    out.location = name;
    out.temp     = temp.toDouble();
    out.hum      = qRound(hum.toDouble());
    out.press    = qRound(press.toDouble());
    out.ws       = details["wind_speed"].toDouble();
    out.rain     = next["details"].toObject()["precipitation_amount"].toDouble();
    out.symbol   = next["summary"].toObject()["symbol_code"].toString();
    return true;
}

QDateTime Weather::parseHttpDate(const QByteArray &value)
{
    QDateTime t = QLocale::c().toDateTime(QString::fromLatin1(value).trimmed(), "ddd, dd MMM yyyy hh:mm:ss 'GMT'");
    t.setTimeSpec(Qt::UTC);
    return t;
}

/* logged once until a request works again */
void Weather::failed(const QString &error)
{
    if(m_lastError.isEmpty())
        qCWarning(lcWeather).noquote() << "Weather request failed:" << error;
    m_lastError = error;
}

void Weather::replyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(m_config.provider == weather_cfg_t::MetNo)
    {
        const QByteArray expires = reply->rawHeader("Expires");
        if(!expires.isEmpty())
            m_expires = parseHttpDate(expires);
        const QByteArray lastModified = reply->rawHeader("Last-Modified");
        if(!lastModified.isEmpty())
            m_lastModified = lastModified;

        /* Not modified: the data shown is still current */
        if(status == 304)
        {
            m_lastOkMs = MonoClock::nowMs();
            m_lastError.clear();
            return;
        }
    }

    if(reply->error() != QNetworkReply::NoError)
    {
        failed(status ? QString("HTTP %1, %2").arg(status).arg(reply->errorString()) : reply->errorString());
        return;
    }

    const QByteArray answer = reply->readAll();
    weather_t info;
    const bool ok = m_config.provider == weather_cfg_t::MetNo
            ? parseMetNo(answer, m_config.location, info)
            : parseWttr(QString::fromUtf8(answer), info);
    if(!ok)
    {
        failed("answer not understood: " + QString::fromUtf8(answer.left(80)));
        return;
    }
    if(!m_lastError.isEmpty())
        qCInfo(lcWeather) << "Weather data again";
    m_lastError.clear();
    m_last = info;
    m_lastOkMs = MonoClock::nowMs();
    qCDebug(lcWeather) << "Weather" << info.location << info.temp << "C" << info.hum << "%" << info.symbol;
    emit newWeather(info);
}
