#include "watchdog.h"
#include "logging.h"
#include <linux/watchdog.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief WatchDog::WatchDog
 * @param parent
 */
WatchDog::WatchDog(QObject *parent) : QObject(parent)
{
    m_fd = -1;
    m_timeoutS = 0;
    m_unhealthy = false;
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &WatchDog::refresh);
}

WatchDog::~WatchDog()
{
    stop();
}

/**
 * @brief WatchDog::start
 * Open the device and refresh it every timeout/4
 * @param device    /dev/watchdog (tests: another path)
 * @return true if the watchdog is running
 */
bool WatchDog::start(const QString &device)
{
    if(m_fd >= 0)
        return true;

    m_fd = ::open(device.toLocal8Bit().constData(), O_WRONLY);
    if(m_fd < 0)
    {
        qCWarning(lcApp).noquote() << "Err open" << device << ", watchdog disabled";
        return false;
    }

    int timeoutS = 0;
    if(::ioctl(m_fd, WDIOC_GETTIMEOUT, &timeoutS) != 0 || timeoutS <= 0)
    {
        qCWarning(lcApp) << "Err ioctl WDIOC_GETTIMEOUT, refresh every 1s";
        timeoutS = 4;
    }

    m_timeoutS = timeoutS;
    const int periodS = qMax(1, timeoutS / 4);
    qCInfo(lcApp) << "WatchDog timeout" << timeoutS << "s, refresh every" << periodS << "s";
    m_timer->start(periodS * 1000);
    refresh();
    return true;
}

/**
 * @brief WatchDog::stop
 * Magic close: 'V' disarms the watchdog (clean exit only)
 */
void WatchDog::stop()
{
    m_timer->stop();
    if(m_fd >= 0)
    {
        qCInfo(lcApp) << "WatchDog stop";
        ssize_t r = ::write(m_fd, "V", 1);
        (void) r;
        ::close(m_fd);
        m_fd = -1;
    }
}

/**
 * @brief WatchDog::setHealthCheck
 * @param check     true when the application works; nullptr: none
 */
void WatchDog::setHealthCheck(std::function<bool()> check)
{
    m_check = check;
    m_unhealthy = false;
}

/**
 * @brief WatchDog::refresh
 * Not refreshed while the health check fails: the board reboots
 */
void WatchDog::refresh()
{
    if(m_fd < 0)
        return;
    if(m_check && !m_check())
    {
        if(!m_unhealthy)
            qCCritical(lcApp) << "Health check failed: watchdog not refreshed, the board will reboot";
        m_unhealthy = true;
        return;
    }
    if(m_unhealthy)
        qCWarning(lcApp) << "Health check back to normal: watchdog refreshed again";
    m_unhealthy = false;
    if(::ioctl(m_fd, WDIOC_KEEPALIVE, 0) != 0)
        qCWarning(lcApp) << "Err ioctl WDIOC_KEEPALIVE";
}
