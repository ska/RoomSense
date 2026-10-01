#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include "common.h"

/*
 * setting.ini: configuration, written only by hand, never by the application.
 * Deployed as setting.default.ini and copied to setting.ini on first start,
 * so a deploy does not overwrite the device configuration (as AcquaThermoNet).
 */
/* [SCREENSAVER] */
typedef struct {
    int     idleS = 30;             /*no touch for this long: screen saver, 0 = off*/
    QString webcamUrl;              /*image in the middle, empty: none*/
    int     webcamRefreshS = 60;    /*reloaded this often while shown*/
} screensaver_t;

/* [WEATHER], as AcquaThermoNet */
typedef struct weather_cfg_t {
    enum Provider { Wttr, MetNo };
    Provider provider = MetNo;
    QString  location;              /*name shown; wttr.in place*/
    double   lat = 0;
    double   lon = 0;
    int      altitude = 0;
    bool     hasCoords = false;
    QString  contact;               /*met.no User-Agent contact*/
    int      pollS = 600;

    /* Enough data to poll the selected provider */
    bool enabled() const { return provider == MetNo ? hasCoords : !location.isEmpty(); }
} weather_cfg_t;

class ConfigFile
{
public:
    explicit ConfigFile(const QString &settingsPath);
    static bool ensureSettings(const QString &settingsPath, const QString &defaultPath);
    int ReadSensorsInfo(QVector<sensor_t> &sensors);
    int ReadMqttInfo(mqtt_brk_t *mqi);
    void ReadLogInfo(QString &path, qint64 &maxBytes, int &files) const;
    screensaver_t ReadScreenSaverInfo() const;
    weather_cfg_t ReadWeatherInfo() const;

private:
    QString m_path;
};

#endif // CONFIGFILE_H
