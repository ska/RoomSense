#include "saverview.h"
#include <QStringList>

QVector<SaverView::line_t> SaverView::summary(const input_t &in)
{
    QVector<line_t> lines;

    /* alarms */
    if(!in.brokerConnected)
        lines.append({ "Network offline", Alarm });
    else if(!in.controllerOnline)
        lines.append({ "Heating controller offline", Alarm });

    QStringList lost, never, battery;
    for(const zone_t &z : in.zones)
    {
        if(z.level == ZoneView::Lost)
            lost << z.name;
        else if(z.level == ZoneView::Never)
            never << z.name;
        else if(z.batt > 0 && z.batt < 20)
            battery << QString("%1 %2 %").arg(z.name).arg(z.batt);
    }
    if(!lost.isEmpty())
        lines.append({ "No sensor: " + lost.join(", "), Alarm });
    if(!never.isEmpty())
        lines.append({ "No reading yet: " + never.join(", "), Alarm });
    if(!battery.isEmpty())
        lines.append({ "Battery low: " + battery.join(", "), Alarm });
    if(lines.isEmpty())
        lines.append({ "All rooms OK", Ok });

    /* house mode; no room heating (the rooms heating are icons) */
    if(in.mode.level == "window" || in.mode.level == "away" || in.mode.level == "boost")
        lines.append({ in.mode.detail.isEmpty() ? in.mode.text : QString::fromUtf8("%1 · %2").arg(in.mode.text, in.mode.detail), Info });
    if(heatingKnown(in) && heating(in).isEmpty())
        lines.append({ "No room heating", Info });
    return lines;
}

bool SaverView::heatingKnown(const input_t &in)
{
    return in.controllerOnline && in.brokerConnected;
}

QStringList SaverView::heating(const input_t &in)
{
    QStringList names;
    if(!heatingKnown(in))
        return names;
    for(const zone_t &z : in.zones)
        if(z.hasZone && z.heat && z.level != ZoneView::Lost)
            names << z.name;
    return names;
}

QString SaverView::weatherIcon(const QString &symbol)
{
    const bool night = symbol.endsWith("_night");
    if(symbol.contains("thunder"))
        return "wx-thunder";
    if(symbol.contains("snow"))
        return "wx-snow";
    if(symbol.contains("rain") || symbol.contains("sleet"))
        return "wx-rain";
    if(symbol == "fog")
        return "wx-fog";
    if(symbol == "cloudy")
        return "wx-cloud";
    if(symbol.startsWith("partlycloudy") || symbol.startsWith("fair"))
        return night ? "wx-partly-night" : "wx-partly-day";
    if(symbol.startsWith("clearsky"))
        return night ? "wx-moon" : "wx-sun";
    return QString();
}

SaverView::weather_view_t SaverView::weather(const weather_t &w, qint64 ageS)
{
    weather_view_t v;
    if(ageS < 0 || ageS > WEATHER_MAX_AGE_S)
        return v;
    v.shown = true;
    v.icon = weatherIcon(w.symbol);
    v.temp = QString::number(w.temp, 'f', 1) + QString::fromUtf8("°");
    QStringList d;
    d << QString("Humidity %1 %").arg(w.hum)
      << QString("Wind %1 m/s").arg(w.ws, 0, 'f', 1);
    if(w.rain > 0)
        d << QString("Rain %1 mm").arg(w.rain, 0, 'f', 1);
    v.details = d.join(QString::fromUtf8(" · "));
    return v;
}
