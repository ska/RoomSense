#include "longpress.h"
#include <QEvent>
#include <QWidget>

LongPress::LongPress(QWidget *watched, int holdMs) : QObject(watched)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(holdMs);
    connect(&m_timer, &QTimer::timeout, this, &LongPress::longPressed);
    watched->installEventFilter(this);
}

bool LongPress::eventFilter(QObject *watched, QEvent *event)
{
    switch(event->type())
    {
    case QEvent::MouseButtonPress:
        m_timer.start();
        break;
    case QEvent::MouseButtonRelease:
    case QEvent::Leave:
    case QEvent::Hide:
        m_timer.stop();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}
