#include "zoneview.h"

QString ZoneView::age(qint64 seconds)
{
    if(seconds < 60)
        return QString("%1 s").arg(qMax<qint64>(0, seconds));
    if(seconds < 3600)
        return QString("%1 min").arg(seconds / 60);
    return QString("%1 h").arg(seconds / 3600);
}

ZoneView::view_t ZoneView::make(const input_t &in)
{
    view_t v;

    /* sensor */
    if(!in.heard)
        v.level = Never;
    else if(in.ageS > LOST_S)
        v.level = Lost;
    else if(in.ageS > STALE_S)
        v.level = Stale;
    else
        v.level = Ok;

    switch(v.level)
    {
    case Never: v.statusText = "no reading yet"; break;
    case Ok:    v.statusText = "updated " + age(in.ageS) + " ago"; break;
    case Stale: v.statusText = "last reading " + age(in.ageS) + " ago"; break;
    case Lost:  v.statusText = "NO SENSOR for " + age(in.ageS); break;
    }

    const bool showReading = (v.level == Ok || v.level == Stale);
    v.tempMuted = !showReading;
    v.tempText = showReading ? QString::number(in.temp / 10.0, 'f', 1) + "°" : QString("--.-°");
    v.humText = (showReading && in.hum > 0 && in.hum <= 100) ? QString("%1 %").arg(in.hum) : QString("--");
    v.battLow = showReading && in.batt > 0 && in.batt < 20;
    if(!showReading || in.batt <= 0 || in.batt > 100)
        v.battText = "--";
    else
        v.battText = v.battLow ? QString("%1 % low").arg(in.batt) : QString("%1 %").arg(in.batt);

    /* heating, as AcquaThermoNet reports it */
    if(v.level == Lost && in.hasZone)
    {
        v.heat = HeatNoSensor;
        v.heatText = "Off, no sensor";
    }
    else if(!in.hasZone || !in.control.hasHeat)
    {
        v.heat = HeatUnknown;
        v.heatText = QString();
    }
    else
    {
        v.heat = in.control.heat ? HeatOn : HeatIdle;
        v.heatText = in.control.heat ? "Heating" : "Idle";
    }

    /* setpoint: AcquaThermoNet owns it */
    if(!in.hasZone)
    {
        v.setPoint = NoZone;
        v.setPointText = "--.-";
        v.setPointLabel = "no zone";
    }
    else if(!in.control.hasSetPoint)
    {
        v.setPoint = NoSetPoint;
        v.setPointText = "--.-";
        v.setPointLabel = "no setpoint";
    }
    else if(!in.controllerOnline)
    {
        v.setPoint = LastKnown;
        v.setPointText = QString::number(in.control.setPoint, 'f', 1);
        v.setPointLabel = "last known";
    }
    else if(in.control.pending)
    {
        v.setPoint = Pending;
        v.setPointText = QString::number(in.control.pendingValue, 'f', 1);
        v.setPointLabel = "sending…";
    }
    else if(in.houseMode != ZoneControl::Normal)
    {
        /* state_temp is the value of the house mode */
        v.setPoint = in.houseMode == ZoneControl::Boost ? InBoost : InMode;
        v.setPointText = QString::number(in.control.setPoint, 'f', 1);
        static const char *labels[] = { "", "windows open", "away", "boost" };
        v.setPointLabel = labels[in.houseMode];
    }
    else
    {
        v.setPoint = Confirmed;
        v.setPointText = QString::number(in.control.setPoint, 'f', 1);
        v.setPointLabel = "setpoint";
    }
    return v;
}

ZoneView::mode_view_t ZoneView::modeButton(const mode_input_t &in)
{
    static const char *icons[] = { "home", "window", "plane", "flame" };
    static const char *levels[] = { "normal", "window", "away", "boost" };
    static const char *texts[] = { "Normal", "Windows open", "Away", "Boost" };
    mode_view_t v;
    if(in.pending)
    {
        v.text = QString::fromUtf8("Sending…");
        v.icon = icons[in.pendingMode];
        v.level = "pending";
    }
    else if(!in.hasMode)
    {
        v.text = "Unknown";
        v.icon = icons[ZoneControl::Normal];
        v.level = "unknown";
    }
    else
    {
        v.text = texts[in.mode];
        v.icon = icons[in.mode];
        v.level = levels[in.mode];
        if(in.mode == ZoneControl::Window || in.mode == ZoneControl::Boost)
        {
            /* minutes rounded up: "1 min left" until the end */
            v.detail = in.remainingS < 60 ? QString("%1 s left").arg(in.remainingS)
                                          : QString("%1 min left").arg((in.remainingS + 59) / 60);
        }
    }
    return v;
}

ZoneView::chrono_view_t ZoneView::chronoLine(const chrono_input_t &in)
{
    chrono_view_t v;
    if(!in.hasZone || !in.chrono.hasState)
        return v;
    v.shown = true;
    v.tappable = in.chrono.hasProgram;
    const ChronoState &s = in.chrono.state;
    if(in.chrono.saving)
    {
        v.text = QString::fromUtf8("saving…");
        v.level = "saving";
        return v;
    }
    if(!s.on)
    {
        v.text = "chrono off";
        v.level = "off";
    }
    else if(s.paused)
    {
        v.text = "chrono paused";
        v.level = "paused";
    }
    else if(!s.hasNext)
    {
        /* the controller clock is not set */
        v.text = "chrono on";
        v.level = "next";
    }
    else if(s.manual)
    {
        v.text = QString::fromUtf8("manual → ") + s.nextChange;
        v.level = "manual";
    }
    else
    {
        v.text = s.nextChange + QString::fromUtf8(" → ") + QString::number(s.nextSetpoint, 'f', 1) + QString::fromUtf8("°");
        v.level = "next";
    }
    /* the last state known: grey, as the setpoints */
    if(!in.controllerOnline)
        v.level = "lastknown";
    return v;
}
