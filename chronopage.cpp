#include "chronopage.h"
#include "chronoview.h"
#include "daychart.h"
#include "zonecard.h"
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

static QLabel *label(const char *objectName, const QString &text, QWidget *parent)
{
    QLabel *l = new QLabel(text, parent);
    l->setObjectName(objectName);
    return l;
}

static QFrame *box(QWidget *parent)
{
    QFrame *f = new QFrame(parent);
    f->setObjectName("chronoBox");
    return f;
}

ChronoPage::ChronoPage(const QVector<sensor_t> &sensors, ZoneControl *control, QWidget *parent) :
    QWidget(parent),
    m_control(control)
{
    setObjectName("chronoPage");
    for(const sensor_t &s : sensors)
    {
        const QString name = QString::fromUtf8(s.name);
        m_names.append(name.isEmpty() ? QString::fromLatin1(s.zone) : name);
    }

    /* header: Back, zone, chrono on/off */
    QPushButton *back = new QPushButton(QString::fromUtf8("‹  Back"), this);
    back->setObjectName("backButton");
    back->setFixedHeight(56);
    back->setFocusPolicy(Qt::NoFocus);
    connect(back, &QPushButton::clicked, this, &ChronoPage::backRequested);
    m_title = label("chronoTitle", QString(), this);
    m_toggle = new QPushButton(this);
    m_toggle->setObjectName("chronoToggle");
    m_toggle->setFixedHeight(56);
    m_toggle->setFocusPolicy(Qt::NoFocus);
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        m_control->setChronoOn(m_zone, !m_toggle->property("on").toBool());
    });
    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(14);
    header->addWidget(back);
    header->addWidget(m_title, 1);
    header->addWidget(m_toggle);

    m_message = label("chronoMessage", QString(), this);
    m_message->hide();
    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    m_messageTimer->setInterval(5000);
    connect(m_messageTimer, &QTimer::timeout, m_message, &QLabel::hide);

    m_none = label("chronoNone", "No chrono program from the heating controller", this);
    m_none->setAlignment(Qt::AlignCenter);
    m_content = new QWidget(this);

    /* today */
    QFrame *todayBox = box(m_content);
    m_todayTitle = label("chronoBoxTitle", QString(), todayBox);
    m_today = new DayChart(todayBox);
    m_today->setFixedHeight(170);
    m_today->setShowValues(true);
    QHBoxLayout *axis = new QHBoxLayout;
    for(const char *h : { "00", "06", "12", "18", "24" })
    {
        axis->addWidget(label("chronoAxis", h, todayBox));
        if(QString(h) != "24")
            axis->addStretch();
    }
    m_next = label("chronoNext", QString(), todayBox);
    QVBoxLayout *t = new QVBoxLayout(todayBox);
    t->setContentsMargins(16, 12, 16, 12);
    t->setSpacing(8);
    t->addWidget(m_todayTitle);
    t->addWidget(m_today);
    t->addLayout(axis);
    t->addWidget(m_next);

    /* the week */
    QFrame *weekBox = box(m_content);
    QGridLayout *week = new QGridLayout(weekBox);
    week->setContentsMargins(16, 12, 16, 12);
    week->setHorizontalSpacing(12);
    week->setVerticalSpacing(8);
    week->addWidget(label("chronoBoxTitle", "This week", weekBox), 0, 0, 1, 3);
    for(int i = 0; i < ChronoView::WEEK_DAYS; i++)
    {
        QLabel *day = label("chronoDay", QString(), weekBox);
        day->setFixedWidth(70);
        DayChart *chart = new DayChart(weekBox);
        chart->setFixedHeight(28);
        QLabel *range = label("chronoRange", QString(), weekBox);
        range->setFixedWidth(110);
        range->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        week->addWidget(day, i + 1, 0);
        week->addWidget(chart, i + 1, 1);
        week->addWidget(range, i + 1, 2);
        m_dayLabels.append(day);
        m_dayCharts.append(chart);
        m_dayRanges.append(range);
    }
    week->setColumnStretch(1, 1);

    /* the two profiles */
    QFrame *weekdayBox = box(m_content);
    QVBoxLayout *wd = new QVBoxLayout(weekdayBox);
    wd->setContentsMargins(16, 12, 16, 12);
    wd->addWidget(label("chronoProfileTitle", "WEEKDAY", weekdayBox));
    m_weekdayLines = label("chronoSlots", QString(), weekdayBox);
    wd->addWidget(m_weekdayLines);
    wd->addStretch();
    QFrame *holidayBox = box(m_content);
    QVBoxLayout *hd = new QVBoxLayout(holidayBox);
    hd->setContentsMargins(16, 12, 16, 12);
    m_holidayTitle = label("chronoProfileTitle", QString(), holidayBox);
    hd->addWidget(m_holidayTitle);
    m_holidayLines = label("chronoSlots", QString(), holidayBox);
    hd->addWidget(m_holidayLines);
    hd->addStretch();
    QHBoxLayout *profiles = new QHBoxLayout;
    profiles->setSpacing(12);
    profiles->addWidget(weekdayBox, 1);
    profiles->addWidget(holidayBox, 1);

    m_footer = label("chronoFooter", QString(), m_content);
    m_edit = new QPushButton(QString::fromUtf8("Edit program"), m_content);
    m_edit->setObjectName("editSave");
    m_edit->setFixedHeight(64);
    m_edit->setFocusPolicy(Qt::NoFocus);
    connect(m_edit, &QPushButton::clicked, this, [this] { emit editRequested(m_zone); });
    QHBoxLayout *footer = new QHBoxLayout;
    footer->addWidget(m_footer, 1);
    footer->addWidget(m_edit);

    QVBoxLayout *c = new QVBoxLayout(m_content);
    c->setContentsMargins(0, 0, 0, 0);
    c->setSpacing(12);
    c->addWidget(todayBox);
    c->addWidget(weekBox);
    c->addLayout(profiles);
    c->addStretch();
    c->addLayout(footer);

    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 12, 16, 16);
    page->setSpacing(12);
    page->addLayout(header);
    page->addWidget(m_message);
    page->addWidget(m_none, 1);
    page->addWidget(m_content, 1);

    connect(m_control, &ZoneControl::chronoChanged, this, [this](int index) {
        if(index == m_zone)
            refresh();
    });
    connect(m_control, &ZoneControl::controllerOnlineChanged, this, &ChronoPage::refresh);
    connect(m_control, &ZoneControl::chronoResult, this, &ChronoPage::onResult);
    /* the time now and the day */
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ChronoPage::refresh);
    timer->start(30 * 1000);
}

void ChronoPage::showZone(int index)
{
    m_zone = index;
    m_message->hide();
    refresh();
}

void ChronoPage::onResult(int index, ZoneControl::ChronoResult result)
{
    if(index != m_zone)
        return;
    m_message->setText(ChronoView::resultText(result));
    setStyleProperty(m_message, "level", result == ZoneControl::ChronoSaved ? "ok" : "bad");
    m_message->show();
    m_messageTimer->start();
}

void ChronoPage::refresh()
{
    if(m_zone < 0 || m_zone >= m_names.size())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const ChronoView::view_t v = ChronoView::make(m_control->chrono(m_zone), m_control->canChangeChrono(m_zone), now.date());

    m_title->setText(m_names.at(m_zone));
    m_none->setVisible(!v.hasProgram);
    m_content->setVisible(v.hasProgram);
    m_toggle->setVisible(v.hasProgram);
    if(!v.hasProgram)
        return;

    m_toggle->setText(v.toggleText);
    m_toggle->setEnabled(v.toggleEnabled);
    m_toggle->setProperty("on", v.toggleOn);
    setStyleProperty(m_toggle, "state", v.toggleOn ? "on" : "off");

    m_todayTitle->setText(v.title);
    m_today->setTemps(v.week.first().temps, v.chartOn);
    m_today->setNow(now.time().hour() * 60 + now.time().minute());
    m_next->setText(v.next);
    for(int i = 0; i < v.week.size(); i++)
    {
        m_dayLabels.at(i)->setText(v.week.at(i).label);
        setStyleProperty(m_dayLabels.at(i), "today", i == 0 ? "true" : "false");
        m_dayCharts.at(i)->setTemps(v.week.at(i).temps, v.chartOn);
        m_dayRanges.at(i)->setText(v.week.at(i).holiday ? "holiday" : v.week.at(i).range);
    }
    m_weekdayLines->setText(v.weekday.isEmpty() ? QString("no slot") : v.weekday.join('\n'));
    m_holidayTitle->setText(v.holidayTitle);
    m_holidayLines->setText(v.holiday.isEmpty() ? QString("as weekday") : v.holiday.join('\n'));
    m_footer->setText(v.footer);
    m_edit->setEnabled(m_control->canChangeChrono(m_zone));
}
