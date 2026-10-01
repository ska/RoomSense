#ifndef ZONECARD_H
#define ZONECARD_H

#include <QFrame>
#include "zoneview.h"

class QLabel;
class QPushButton;

/* Dynamic property for the stylesheet, repolished only when it changes */
void setStyleProperty(QWidget *w, const char *name, const QString &value);

/*
 * One zone on the main page, sized for the touch panel (as the
 * AcquaThermoNet ZoneCard). Only shows the ZoneView it is given; the
 * colors come from the stylesheet through the dynamic properties
 * "level", "heat", "setpoint" and "low".
 */
class ZoneCard : public QFrame
{
    Q_OBJECT
public:
    explicit ZoneCard(const QString &name, QWidget *parent = nullptr);

    void display(const ZoneView::view_t &v, bool canChange);

signals:
    void minusClicked();
    void plusClicked();

private:
    QLabel      *m_name;
    QLabel      *m_heat;
    QLabel      *m_temp;
    QLabel      *m_setPoint;
    QLabel      *m_setPointLabel;
    QLabel      *m_hum;
    QLabel      *m_batt;
    QLabel      *m_status;
    QPushButton *m_minus;
    QPushButton *m_plus;
};

#endif // ZONECARD_H
