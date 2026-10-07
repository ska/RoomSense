#include "chrono.h"
#include "zonecontrol.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <algorithm>

/* Local date and time; a time skipped by the DST change starts an hour later */
static QDateTime localAt(const QDate &date, const QTime &time)
{
    QDateTime dt(date, time);
    if(!dt.isValid())
        dt = QDateTime(date, time.addSecs(3600));
    return dt;
}

bool Chrono::isHoliday(const QDate &date, const ChronoConfig &config)
{
    return config.holidayDays & (1 << date.dayOfWeek());
}

const QVector<ChronoSlot> &Chrono::profile(const QDate &date, const ChronoConfig &config)
{
    if(isHoliday(date, config) && !config.holiday.isEmpty())
        return config.holiday;
    return config.weekday;
}

QString Chrono::profileName(const QDate &date, const ChronoConfig &config)
{
    return &profile(date, config) == &config.holiday ? "holiday" : "weekday";
}

ChronoPoint Chrono::current(const QDateTime &now, const ChronoConfig &config)
{
    ChronoPoint p;
    if(!config.enabled || config.weekday.isEmpty())
        return p;

    const QDate today = now.date();
    const QVector<ChronoSlot> &day = profile(today, config);
    for(int i = day.size() - 1; i >= 0; i--)
    {
        if(day[i].at <= now.time())
        {
            p.temp  = day[i].temp;
            p.start = localAt(today, day[i].at);
            return p;
        }
    }

    /* Before the first slot: every profile has slots (empty holiday = weekday) */
    const QDate yesterday = today.addDays(-1);
    const ChronoSlot &last = profile(yesterday, config).last();
    p.temp  = last.temp;
    p.start = localAt(yesterday, last.at);
    return p;
}

ChronoPoint Chrono::next(const QDateTime &now, const ChronoConfig &config)
{
    ChronoPoint p;
    if(!config.enabled || config.weekday.isEmpty())
        return p;

    const QDate today = now.date();
    for(const ChronoSlot &s : profile(today, config))
    {
        if(s.at > now.time())
        {
            p.temp  = s.temp;
            p.start = localAt(today, s.at);
            return p;
        }
    }

    const QDate tomorrow = today.addDays(1);
    const ChronoSlot &first = profile(tomorrow, config).first();
    p.temp  = first.temp;
    p.start = localAt(tomorrow, first.at);
    return p;
}

QVector<double> Chrono::dayTemps(const QDate &date, const ChronoConfig &config, int stepMin)
{
    QVector<double> out;
    if(config.weekday.isEmpty() || stepMin <= 0)
        return out;
    /* Wall clock times, no QDateTime: a time skipped by DST is still an interval */
    const QVector<ChronoSlot> &day = profile(date, config);
    double temp = profile(date.addDays(-1), config).last().temp;
    int next = 0;
    for(int m = 0; m < 24 * 60; m += stepMin)
    {
        const QTime t(m / 60, m % 60);
        while(next < day.size() && day[next].at <= t)
            temp = day[next++].temp;
        out.append(temp);
    }
    return out;
}

static const int DAY_MIN = 24 * 60;

static int minutes(const QTime &t)
{
    return t.hour() * 60 + t.minute();
}

static QTime fromMinutes(int m)
{
    return QTime(m / 60, m % 60);
}

/**
 * @brief Chrono::stepTime
 * Slot k to the next (dir > 0) or previous point of the 15-minute grid,
 * strictly between its neighbours: the order of the slots never changes
 */
bool Chrono::stepTime(QVector<ChronoSlot> &list, int k, int dir)
{
    if(k < 0 || k >= list.size() || dir == 0)
        return false;
    const int m = minutes(list[k].at);
    const int target = dir > 0 ? (m / EDIT_STEP_MIN + 1) * EDIT_STEP_MIN
                               : ((m + EDIT_STEP_MIN - 1) / EDIT_STEP_MIN - 1) * EDIT_STEP_MIN;
    const int prev = k > 0 ? minutes(list[k-1].at) : -1;
    const int next = k + 1 < list.size() ? minutes(list[k+1].at) : DAY_MIN;
    if(target < 0 || target <= prev || target >= next)
        return false;
    list[k].at = fromMinutes(target);
    return true;
}

bool Chrono::stepTemp(QVector<ChronoSlot> &list, int k, int dir)
{
    if(k < 0 || k >= list.size() || dir == 0)
        return false;
    const double t = list[k].temp + (dir > 0 ? ZoneControl::STEP : -ZoneControl::STEP);
    if(t < ZoneControl::MIN || t > ZoneControl::MAX)
        return false;
    list[k].temp = t;
    return true;
}

int Chrono::splitSlot(QVector<ChronoSlot> &list, int k)
{
    if(list.isEmpty())
    {
        list.append({ QTime(8, 0), 20.0 });
        return 0;
    }
    if(k < 0 || k >= list.size() || list.size() >= ChronoConfig::MAX_SLOTS)
        return -1;
    const int start = minutes(list[k].at);
    const int end = k + 1 < list.size() ? minutes(list[k+1].at) : DAY_MIN;
    if(end - start < 2 * EDIT_STEP_MIN)
        return -1;
    int at = (start + (end - start) / 2) / EDIT_STEP_MIN * EDIT_STEP_MIN;
    if(at <= start)
        at += EDIT_STEP_MIN;
    if(at >= end)
        return -1;
    list.insert(k + 1, { fromMinutes(at), list[k].temp });
    return k + 1;
}

/* A profile of the program: [{"at":"HH:MM","temp":20.5}, ...] */
static bool slotsFromJson(const QJsonValue &value, QVector<ChronoSlot> &out)
{
    if(!value.isArray())
        return false;
    out.clear();
    for(const QJsonValue &v : value.toArray())
    {
        const QJsonObject o = v.toObject();
        ChronoSlot s;
        s.at = QTime::fromString(o.value("at").toString(), "HH:mm");
        if(!s.at.isValid() || !o.value("temp").isDouble())
            return false;
        s.temp = o.value("temp").toDouble();
        out.append(s);
    }
    std::sort(out.begin(), out.end(), [](const ChronoSlot &a, const ChronoSlot &b) { return a.at < b.at; });
    return out.size() <= ChronoConfig::MAX_SLOTS;
}

static QJsonArray slotsJson(const QVector<ChronoSlot> &profile)
{
    QJsonArray out;
    for(const ChronoSlot &s : profile)
        out.append(QJsonObject{ { "at", s.at.toString("HH:mm") }, { "temp", s.temp } });
    return out;
}

bool Chrono::parseProgram(const QByteArray &payload, ChronoConfig &config)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if(!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    if(!obj.value("enabled").isBool() || !obj.value("holiday_days").isArray())
        return false;

    ChronoConfig c;
    c.enabled = obj.value("enabled").toBool();
    c.edited  = obj.value("edited").toBool();
    for(const QJsonValue &d : obj.value("holiday_days").toArray())
    {
        const int day = d.toInt();
        if(day < 1 || day > 7)
            return false;
        c.holidayDays |= 1 << day;
    }
    if(!slotsFromJson(obj.value("weekday"), c.weekday) || !slotsFromJson(obj.value("holiday"), c.holiday))
        return false;
    config = c;
    return true;
}

bool Chrono::parseState(const QByteArray &payload, ChronoState &state)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if(!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    const QString chrono = obj.value("chrono").toString();
    if(chrono != "on" && chrono != "off")
        return false;

    ChronoState s;
    s.on      = chrono == "on";
    s.paused  = obj.value("paused").toBool();
    s.manual  = obj.value("manual").toBool();
    s.profile = obj.value("profile").toString();
    const QTime next = QTime::fromString(obj.value("next_change").toString(), "HH:mm");
    s.hasNext = s.on && next.isValid() && obj.value("next_setpoint").isDouble();
    if(s.hasNext)
    {
        s.nextChange   = next.toString("HH:mm");
        s.nextSetpoint = obj.value("next_setpoint").toDouble();
    }
    state = s;
    return true;
}

QByteArray Chrono::programCommand(const ChronoConfig &config)
{
    QJsonObject obj;
    obj.insert("enabled", config.enabled);
    obj.insert("weekday", slotsJson(config.weekday));
    obj.insert("holiday", slotsJson(config.holiday));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

/* As the controller stores a profile: sorted, setpoints rounded to 0.5 */
static QVector<ChronoSlot> stored(QVector<ChronoSlot> list)
{
    for(ChronoSlot &s : list)
        s.temp = ZoneControl::normalize(s.temp);
    std::sort(list.begin(), list.end(), [](const ChronoSlot &a, const ChronoSlot &b) { return a.at < b.at; });
    return list;
}

bool Chrono::sameProgram(const ChronoConfig &a, const ChronoConfig &b)
{
    return a.enabled == b.enabled
        && stored(a.weekday) == stored(b.weekday)
        && stored(a.holiday) == stored(b.holiday);
}

QString Chrono::toString(const QVector<ChronoSlot> &profile)
{
    QStringList parts;
    for(const ChronoSlot &s : profile)
        parts.append(s.at.toString("HH:mm") + "=" + QString::number(s.temp, 'f', 1));
    return parts.join(", ");
}
