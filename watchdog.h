#ifndef WATCHDOG_H
#define WATCHDOG_H
#include <QObject>
#include <QTimer>
#include <functional>
#include "common.h"

/*
 * Hardware watchdog (/dev/watchdog), refreshed by a QTimer in the thread
 * that owns it, as AcquaThermoNet. Must live in the main thread: if the
 * event loop hangs the refresh stops and the board reboots. An optional
 * health check (the BLE thread alive) is asked before every refresh: when
 * it fails the refresh stops too.
 */
class WatchDog: public QObject
{
    Q_OBJECT
public:
    explicit WatchDog(QObject *parent = nullptr);
    ~WatchDog();

    bool start(const QString &device = "/dev/watchdog");
    void stop();
    bool isActive() const { return m_fd >= 0; }
    /* Timeout of the device, s (0 when not active) */
    int timeoutS() const { return isActive() ? m_timeoutS : 0; }

    /* Asked before every refresh; nullptr: none */
    void setHealthCheck(std::function<bool()> check);

private slots:
    void refresh();

private:
    int     m_fd;
    int     m_timeoutS;
    QTimer  *m_timer;
    std::function<bool()> m_check;
    bool    m_unhealthy;
};

#endif // WATCHDOG_H
