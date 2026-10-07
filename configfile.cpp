#include "configfile.h"
#include "bleparse.h"
#include <QRegularExpression>
#include <QUrl>

/*
 * Zone name, as AcquaThermoNet Configuration::validZoneName: used in MQTT
 * topics, no separators, wildcards or spaces
 */
static bool validZoneName(const QString &name)
{
    static const QRegularExpression re("^[A-Za-z0-9_-]+$");
    return re.match(name).hasMatch() && name != "list" && name != "mode";
}

/*
 * Copy a string value of the ini into a fixed size buffer, always
 * NUL terminated; a longer value is truncated and reported.
 */
static void copyValue(const QSettings &settings, const QString &key, char *dst, int size)
{
    const QByteArray value = settings.value(key).toString().toUtf8();
    if( value.size() >= size )
        qCWarning(lcConfig).noquote() << QString("Config key %1: longer than %2 characters, truncated").arg(key).arg(size - 1);
    qstrncpy( dst, value.constData(), size );
}

/**
 * @brief ConfigFile::ConfigFile
 * @param settingsPath  setting.ini
 */
ConfigFile::ConfigFile(const QString &settingsPath) :
    m_path(settingsPath)
{
    qCInfo(lcConfig).noquote() << "Configuration:" << m_path;
}

/**
 * @brief ConfigFile::ensureSettings
 * @param settingsPath
 * @param defaultPath   deployed template (setting.default.ini)
 * @return true if settingsPath exists afterwards
 */
bool ConfigFile::ensureSettings(const QString &settingsPath, const QString &defaultPath)
{
    if(QFile::exists(settingsPath))
        return true;
    if(!QFile::copy(defaultPath, settingsPath))
    {
        qCWarning(lcConfig).noquote() << "No" << settingsPath << "and cannot create it from" << defaultPath;
        return false;
    }
    /* copy keeps the read-only mode of a deployed file: it is edited by hand */
    QFile::setPermissions(settingsPath, QFile::permissions(settingsPath) | QFileDevice::WriteOwner);
    qCInfo(lcConfig).noquote() << "Created" << settingsPath << "from" << defaultPath;
    return true;
}

int ConfigFile::ReadSensorsInfo( QVector<sensor_t> &sensors )
{
    quint8 count = 0;
    QStringList zones;
    sensors.clear();

    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("SENSORS");
    foreach (const QString &group, settings.childGroups())
    {
        sensor_t tmp;
        memset(&tmp, 0, sizeof(sensor_t) );
        settings.beginGroup(group);
        const QStringList childKeys = settings.childKeys();

        foreach (const QString &childKey, childKeys)
        {

            if("mac" == childKey)
            {
                /* upper case, as the BLE reader compares it */
                const QString mac = BleParse::normalizeMac( settings.value(childKey).toString() );
                if( mac.isEmpty() )
                    qCWarning(lcConfig).noquote() << QString("Sensor %1: invalid MAC '%2', never read").arg(group, settings.value(childKey).toString());
                else
                    qstrncpy( tmp.smac, mac.toLatin1().constData(), sizeof(tmp.smac) );
#ifdef ENABLE_BLE
                str2ba( tmp.smac, &tmp.mac);
#endif
            }
            else
            if("name" == childKey)
            {
                copyValue( settings, childKey, tmp.name, sizeof(tmp.name) );
            }
            else
            if("polltime" == childKey)
            {
                tmp.polltime = settings.value(childKey).toInt();
            }
        }
        tmp.id = count;

        /* Zone name of AcquaThermoNet (topic RoomSense/<zone>/data),
         * case sensitive; default: the sensor name */
        const QString zone = settings.value("zone", QString::fromUtf8(tmp.name)).toString().trimmed();
        if( !validZoneName(zone) || zone.size() >= (int)sizeof(tmp.zone) )
            qCWarning(lcConfig).noquote() << QString("Sensor %1: invalid zone name '%2', not published").arg(group, zone);
        /* one sensor per zone: a second one would publish on the same topic */
        else if( zones.contains(zone) )
            qCWarning(lcConfig).noquote() << QString("Sensor %1: zone '%2' already used by another sensor, not published").arg(group, zone);
        else
        {
            qstrncpy( tmp.zone, zone.toLatin1().constData(), sizeof(tmp.zone) );
            zones.append(zone);
        }

        settings.endGroup();
        sensors.append(tmp);
        count++;

    }
    settings.endGroup();
    return count;

}

int ConfigFile::ReadMqttInfo( mqtt_brk_t *mqi )
{

    qCDebug(lcConfig) << "ConfigFile::ReadMqttInfo";

    QSettings settings(m_path, QSettings::IniFormat);
    /* defaults, as AcquaThermoNet */
    mqi->port = 1883;
    /* AcquaThermoNet: one message per zone every 60-300 s */
    mqi->publishInterval = 120;

    settings.beginGroup("MQTT");
    const QStringList childKeys = settings.childKeys();
    foreach (const QString &childKey, childKeys)
    {
        /* credentials are never logged */
        if("broker_password" == childKey)
            qCDebug(lcConfig) << " Key: " << childKey << " value: <hidden>";
        else
            qCDebug(lcConfig) << " Key: " << childKey << " value: " << settings.value(childKey).toString();

        if("broker_addr" == childKey)
        {
            copyValue( settings, childKey, mqi->address, sizeof(mqi->address) );
        }
        else
        if("broker_port" == childKey)
        {
            mqi->port = settings.value(childKey).toInt();
        }
        else if("broker_uname" == childKey)
        {
            copyValue( settings, childKey, mqi->uname, sizeof(mqi->uname) );
        }
        else if("broker_password" == childKey)
        {
            copyValue( settings, childKey, mqi->password, sizeof(mqi->password) );
        }
        else if("unique_id" == childKey)
        {
            copyValue( settings, childKey, mqi->uniqueId, sizeof(mqi->uniqueId) );
        }
        else if("publish_interval_s" == childKey)
        {
            bool ok;
            const int v = settings.value(childKey).toInt(&ok);
            if( ok && v >= 60 && v <= 300 )
                mqi->publishInterval = v;
            else
                qCWarning(lcConfig) << "Invalid MQTT/publish_interval_s, using" << mqi->publishInterval;
        }
    }

    settings.endGroup();

    /* empty broker_addr: localhost */
    if( mqi->address[0] == '\0' )
        qstrncpy( mqi->address, "localhost", sizeof(mqi->address) );


    return 0;
}

/**
 * @brief ConfigFile::ReadLogInfo
 * [LOG] file (relative to setting.ini, empty: no file), max_kb, files;
 * as AcquaThermoNet
 */
void ConfigFile::ReadLogInfo(QString &path, qint64 &maxBytes, int &files) const
{
    QSettings settings(m_path, QSettings::IniFormat);
    path     = settings.value("LOG/file").toString().trimmed();
    maxBytes = settings.value("LOG/max_kb", 512).toLongLong() * 1024;
    files    = settings.value("LOG/files", 3).toInt();
    if(!path.isEmpty())
        path = QDir(QFileInfo(m_path).absolutePath()).filePath(path);
}

/**
 * @brief ConfigFile::ReadScreenSaverInfo
 * [SCREENSAVER] idle_s (0 = off, else 10..3600), webcam_url (http, https
 * or file; empty: no image), webcam_refresh_s (10..3600)
 */
screensaver_t ConfigFile::ReadScreenSaverInfo() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("SCREENSAVER");
    screensaver_t ss;
    bool ok;

    int v = settings.value("idle_s", ss.idleS).toInt(&ok);
    if(ok && (v == 0 || (v >= 10 && v <= 3600)))
        ss.idleS = v;
    else
        qCWarning(lcConfig) << "Invalid SCREENSAVER/idle_s, using" << ss.idleS;

    v = settings.value("webcam_refresh_s", ss.webcamRefreshS).toInt(&ok);
    if(ok && v >= 10 && v <= 3600)
        ss.webcamRefreshS = v;
    else
        qCWarning(lcConfig) << "Invalid SCREENSAVER/webcam_refresh_s, using" << ss.webcamRefreshS;

    const QString url = settings.value("webcam_url").toString().trimmed();
    const QUrl u(url);
    if(u.isValid() && (u.scheme() == "http" || u.scheme() == "https" || u.scheme() == "file"))
        ss.webcamUrl = url;
    else if(!url.isEmpty())
        qCWarning(lcConfig) << "Invalid SCREENSAVER/webcam_url, no image";
    settings.endGroup();
    return ss;
}

/**
 * @brief ConfigFile::ReadWeatherInfo
 * [WEATHER] provider (metno|wttr), location, lat/lon/altitude, contact,
 * poll_s (default 600, at least 60); as AcquaThermoNet
 */
weather_cfg_t ConfigFile::ReadWeatherInfo() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("WEATHER");
    weather_cfg_t wc;

    const QString provider = settings.value("provider", "metno").toString().trimmed().toLower();
    if(provider == "wttr")
        wc.provider = weather_cfg_t::Wttr;
    else if(provider != "metno")
        qCWarning(lcConfig) << "Unknown WEATHER/provider" << provider << ", using metno";

    wc.location = settings.value("location").toString().trimmed();
    bool okLat, okLon, ok;
    wc.lat      = settings.value("lat").toDouble(&okLat);
    wc.lon      = settings.value("lon").toDouble(&okLon);
    wc.hasCoords = okLat && okLon;
    wc.altitude = settings.value("altitude", 0).toInt();
    wc.contact  = settings.value("contact").toString().trimmed();
    const int poll = settings.value("poll_s", wc.pollS).toInt(&ok);
    if(ok && poll >= 60)
        wc.pollS = poll;
    else
        qCWarning(lcConfig) << "Invalid WEATHER/poll_s, using" << wc.pollS;
    settings.endGroup();

    if(wc.provider == weather_cfg_t::MetNo && !wc.hasCoords)
        qCInfo(lcConfig) << "WEATHER provider metno without lat and lon: weather disabled";
    return wc;
}
