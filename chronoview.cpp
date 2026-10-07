#include "chronoview.h"
#include <QLocale>
#include <algorithm>

QString ChronoView::dayLabel(const QDate &date, const QDate &today)
{
    if(date == today)
        return "Today";
    return QLocale(QLocale::English).toString(date, "ddd d");
}

QString ChronoView::range(const QVector<double> &temps)
{
    if(temps.isEmpty())
        return QString();
    const auto mm = std::minmax_element(temps.cbegin(), temps.cend());
    if(*mm.first == *mm.second)
        return QString::number(*mm.first, 'f', 1);
    return QString::number(*mm.first, 'f', 1) + QString::fromUtf8(" – ") + QString::number(*mm.second, 'f', 1);
}

QStringList ChronoView::slotLines(const QVector<ChronoSlot> &profile)
{
    QStringList out;
    for(const ChronoSlot &s : profile)
        out.append(s.at.toString("HH:mm") + "   " + QString::number(s.temp, 'f', 1) + QString::fromUtf8("°"));
    return out;
}

QString ChronoView::daysText(quint8 holidayDays)
{
    static const char *names[] = { "", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
    QStringList out;
    for(int d = 1; d <= 7; d++)
        if(holidayDays & (1 << d))
            out.append(names[d]);
    return out.isEmpty() ? QString("none") : out.join(' ');
}

QString ChronoView::resultText(ZoneControl::ChronoResult result)
{
    switch(result)
    {
    case ZoneControl::ChronoSaved:      return "Saved";
    case ZoneControl::ChronoRefused:    return "Not saved: refused by the controller";
    default:                            return "Not saved: no answer from the controller";
    }
}

ChronoView::view_t ChronoView::make(const ZoneControl::chrono_t &chrono, bool canChange, const QDate &today)
{
    view_t v;
    v.hasProgram = chrono.hasProgram;
    if(!v.hasProgram)
        return v;
    const ChronoConfig &p = chrono.program;
    const bool on = chrono.hasState ? chrono.state.on : p.enabled;

    v.chartOn = on;
    v.title = "Today · " + Chrono::profileName(today, p);
    if(chrono.saving)
        v.next = QString::fromUtf8("saving…");
    else if(!on)
        v.next = "chrono off";
    else if(chrono.hasState && chrono.state.paused)
        v.next = "chrono paused (away)";
    else if(chrono.hasState && chrono.state.hasNext)
        v.next = (chrono.state.manual ? QString::fromUtf8("manual setpoint, next: ") : QString("next: "))
                 + chrono.state.nextChange + QString::fromUtf8(" → ")
                 + QString::number(chrono.state.nextSetpoint, 'f', 1) + QString::fromUtf8("°");
    else
        v.next = "chrono on";

    for(int i = 0; i < WEEK_DAYS; i++)
    {
        day_t d;
        d.date = today.addDays(i);
        d.label = dayLabel(d.date, today);
        d.temps = Chrono::dayTemps(d.date, p);
        d.range = range(d.temps);
        d.holiday = Chrono::profileName(d.date, p) == "holiday";
        v.week.append(d);
    }

    v.weekday = slotLines(p.weekday);
    v.holidayTitle = "HOLIDAY · " + daysText(p.holidayDays);
    v.holiday = slotLines(p.holiday);
    v.footer = p.edited ? "Program edited on a panel" : "Program of the controller configuration";

    v.toggleOn = on;
    v.toggleText = chrono.saving ? QString::fromUtf8("Saving…") : (on ? QString("Chrono ON") : QString("Chrono OFF"));
    /* on needs a weekday slot (the controller refuses it) */
    v.toggleEnabled = canChange && (on || !p.weekday.isEmpty());
    return v;
}
