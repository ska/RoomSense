#ifndef CHRONOVIEW_H
#define CHRONOVIEW_H

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>
#include "zonecontrol.h"

/*
 * What the chrono page shows for one zone (AcquaThermoNet program and
 * state, interface §9). No widgets here: ChronoPage only displays it,
 * the rules are unit tested.
 */
namespace ChronoView
{
    /* Days on the page: today and the next six */
    const int WEEK_DAYS = 7;

    typedef struct {
        QDate   date;
        QString label;                  /*"Today", "Wed 8"*/
        QVector<double> temps;          /*96 quarters of an hour*/
        QString range;                  /*"17.0 – 20.5", "20.0"*/
        bool    holiday = false;        /*the holiday profile is used*/
    } day_t;

    typedef struct {
        bool    hasProgram = false;     /*no program from the controller: nothing else*/
        QString title;                  /*"Today · weekday"*/
        QString next;                   /*"next: 22:30 → 17.0°", "chrono off", ...*/
        QVector<day_t> week;            /*WEEK_DAYS days from today*/
        QStringList weekday;            /*"06:30   20.5°"*/
        QString holidayTitle;           /*"HOLIDAY · SAT SUN"*/
        QStringList holiday;            /*empty: "as weekday"*/
        QString footer;                 /*who made the program*/
        QString toggleText;             /*"Chrono ON", "Chrono OFF", "Saving…"*/
        bool    toggleOn = false;
        bool    toggleEnabled = false;
        bool    chartOn = false;        /*the program runs: colored, else grey*/
    } view_t;

    view_t make(const ZoneControl::chrono_t &chrono, bool canChange, const QDate &today);

    /* "Today", "Wed 8" */
    QString dayLabel(const QDate &date, const QDate &today);
    /* "17.0 – 20.5", or one value */
    QString range(const QVector<double> &temps);
    /* "06:30   20.5°" per slot */
    QStringList slotLines(const QVector<ChronoSlot> &profile);
    /* "SAT SUN", "none" */
    QString daysText(quint8 holidayDays);
    /* Message after a change from the panel */
    QString resultText(ZoneControl::ChronoResult result);
}

#endif // CHRONOVIEW_H
