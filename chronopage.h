#ifndef CHRONOPAGE_H
#define CHRONOPAGE_H

#include <QWidget>
#include "common.h"
#include "zonecontrol.h"

class DayChart;
class QGridLayout;
class QLabel;
class QPushButton;
class QTimer;

/*
 * Chrono page of one zone, opened by a tap on its card: today's program
 * with the time now, the next change, the week, the two profiles and the
 * chrono on/off (through ZoneControl, owned by AcquaThermoNet). Texts
 * from ChronoView, colors from the stylesheet.
 */
class ChronoPage : public QWidget
{
    Q_OBJECT
public:
    ChronoPage(const QVector<sensor_t> &sensors, ZoneControl *control, QWidget *parent = nullptr);

    void showZone(int index);
    int zone() const { return m_zone; }

public slots:
    void refresh();

signals:
    void backRequested();
    /* Edit program pressed */
    void editRequested(int index);

private:
    void onResult(int index, ZoneControl::ChronoResult result);

    ZoneControl         *m_control;
    QStringList          m_names;
    int                  m_zone = -1;

    QLabel      *m_title;
    QPushButton *m_toggle;
    QLabel      *m_message;
    QTimer      *m_messageTimer;
    QWidget     *m_content;
    QLabel      *m_none;
    QLabel      *m_todayTitle;
    QLabel      *m_next;
    DayChart    *m_today;
    QVector<QLabel *>   m_dayLabels;
    QVector<DayChart *> m_dayCharts;
    QVector<QLabel *>   m_dayRanges;
    QLabel      *m_weekdayLines;
    QLabel      *m_holidayTitle;
    QLabel      *m_holidayLines;
    QLabel      *m_footer;
    QPushButton *m_edit;
};

#endif // CHRONOPAGE_H
