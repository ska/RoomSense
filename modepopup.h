#ifndef MODEPOPUP_H
#define MODEPOPUP_H

#include <QIcon>
#include <QWidget>
#include "zonecontrol.h"

class QLabel;
class QTimer;
class QPushButton;
class QToolButton;

/* Icon from the resources (:/icons/<name>.svg), drawn with QSvgRenderer:
 * needs QtSvg only, not the image format plugins. color: instead of the
 * light gray of the icons (e.g. "#F28C28"), empty: as drawn */
QIcon modeIcon(const QString &name, int size, const QString &color = QString());

/*
 * House popup of the main page: covers the page (dimmed), a sheet at the
 * bottom with the setpoint of all the rooms (-/+: aligned to the lowest /
 * highest, then by 0.5, ZoneControl::stepAll; the popup stays open) and
 * the three house modes: boost, windows open and away (a press on the
 * active one goes back to normal, ZoneControl::requestMode). A press
 * outside the sheet or on Close closes it, and so do IDLE_CLOSE_MS
 * without a press on the sheet.
 */
class ModePopup : public QWidget
{
    Q_OBJECT
public:
    static const int IDLE_CLOSE_MS = 10000;

    ModePopup(ZoneControl *control, QWidget *parent);
    /* Tests: a shorter time without presses */
    void setIdleCloseMs(int ms);

public slots:
    /* Over the whole parent */
    void open();
    void refresh();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void choose(ZoneControl::HouseMode mode);
    void stepAll(int direction);

    ZoneControl *m_control;
    QWidget     *m_sheet;
    QPushButton *m_allMinus;
    QPushButton *m_allPlus;
    QLabel      *m_allValue;
    QToolButton *m_boost;
    QToolButton *m_window;
    QToolButton *m_away;
    QLabel      *m_note;
    QTimer      *m_idleTimer;
};

#endif // MODEPOPUP_H
