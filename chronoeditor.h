#ifndef CHRONOEDITOR_H
#define CHRONOEDITOR_H

#include <QWidget>
#include "common.h"
#include "zonecontrol.h"

class DayChart;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;

/*
 * Chrono program editor of one zone (variant E2): the profile on a large
 * chart, the slots as chips, one slot selected and changed with big -/+
 * for its time (15 min) and setpoint (0.5), previous/next, split,
 * delete; weekday/holiday, on/off, copy to holiday, reset to the
 * controller's. Edits a copy: Save sends the whole program through
 * ZoneControl (AcquaThermoNet stores it), Cancel drops it. A program
 * changed elsewhere meanwhile is reported before Save overwrites it.
 */
class ChronoEditor : public QWidget
{
    Q_OBJECT
public:
    ChronoEditor(const QVector<sensor_t> &sensors, ZoneControl *control, QWidget *parent = nullptr);

    /* Start editing the program of the zone (as last received) */
    void edit(int index);
    int zone() const { return m_zone; }

    /* Tests */
    const ChronoConfig &program() const { return m_edit; }
    bool holidayShown() const { return m_holiday; }
    int selected() const { return m_selected; }

signals:
    /* Saved, reset or cancelled: back to the chrono page of the zone */
    void done(int index);

private:
    QVector<ChronoSlot> &profile();
    void select(int k);
    void showProfile(bool holiday);
    void changed();
    void refresh();
    void save();

    ZoneControl         *m_control;
    QStringList          m_names;
    int                  m_zone = -1;
    ChronoConfig         m_edit;
    int                  m_version = 0;     /*program version when the edit started*/
    bool                 m_warned = false;  /*changed elsewhere: Save pressed once*/
    bool                 m_holiday = false; /*holiday profile shown*/
    int                  m_selected = -1;

    QLabel      *m_title;
    QPushButton *m_save;
    QPushButton *m_weekdayTab;
    QPushButton *m_holidayTab;
    QPushButton *m_onOff;
    QLabel      *m_warning;
    DayChart    *m_chart;
    QLabel      *m_asWeekday;
    QHBoxLayout *m_chipsLayout;
    QVector<QPushButton *> m_chips;
    QLabel      *m_time;
    QLabel      *m_temp;
    QPushButton *m_timeMinus;
    QPushButton *m_timePlus;
    QPushButton *m_tempMinus;
    QPushButton *m_tempPlus;
    QPushButton *m_prev;
    QPushButton *m_next;
    QPushButton *m_split;
    QPushButton *m_delete;
    QPushButton *m_copy;
    QPushButton *m_reset;
};

#endif // CHRONOEDITOR_H
