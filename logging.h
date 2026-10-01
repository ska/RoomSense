#ifndef LOGGING_H
#define LOGGING_H

#include <QLoggingCategory>
#include <QString>
#include <QStringList>

/*
 * Log categories, as AcquaThermoNet: info and above are on by default,
 * debug with e.g.
 *   QT_LOGGING_RULES="rs.ble.debug=true"
 * (rs.ble debug: every sensor reading)
 */
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcConfig)
Q_DECLARE_LOGGING_CATEGORY(lcMqtt)
Q_DECLARE_LOGGING_CATEGORY(lcBle)
Q_DECLARE_LOGGING_CATEGORY(lcWeather)

namespace Logging
{
    /* Message pattern and handler: every message goes to stderr */
    void install();

    /* Also write info and above to path, rotated at maxBytes keeping
     * path.1 .. path.<files>. Empty path: no file. */
    void enableFile(const QString &path, qint64 maxBytes, int files);

    /* The log file, empty: none */
    QString filePath();

    /* Last messages, info and above, "hh:mm:ss W category: message",
     * newest first, at most RECENT_MAX (Log tab of the diagnostic page) */
    const int RECENT_MAX = 200;
    QStringList recent();
    /* Grows with every message kept: the list changed when it differs */
    quint64 recentCount();
}

#endif // LOGGING_H
