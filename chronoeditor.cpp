#include "chronoeditor.h"
#include "chronoview.h"
#include "daychart.h"
#include "zonecard.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

static QLabel *label(const char *objectName, const QString &text, QWidget *parent)
{
    QLabel *l = new QLabel(text, parent);
    l->setObjectName(objectName);
    return l;
}

static QPushButton *button(const char *objectName, const QString &text, QWidget *parent, int height)
{
    QPushButton *b = new QPushButton(text, parent);
    b->setObjectName(objectName);
    b->setFixedHeight(height);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

/* -/+ of the selected slot: repeat while held */
static QPushButton *stepButton(const QString &text, const QString &name, QWidget *parent)
{
    QPushButton *b = button("editStep", text, parent, 72);
    b->setFixedWidth(88);
    b->setAccessibleName(name);
    b->setAutoRepeat(true);
    b->setAutoRepeatDelay(400);
    b->setAutoRepeatInterval(150);
    return b;
}

ChronoEditor::ChronoEditor(const QVector<sensor_t> &sensors, ZoneControl *control, QWidget *parent) :
    QWidget(parent),
    m_control(control)
{
    setObjectName("chronoEditor");
    for(const sensor_t &s : sensors)
    {
        const QString name = QString::fromUtf8(s.name);
        m_names.append(name.isEmpty() ? QString::fromLatin1(s.zone) : name);
    }

    /* header: Cancel, zone, Save */
    QPushButton *cancel = button("backButton", "Cancel", this, 56);
    connect(cancel, &QPushButton::clicked, this, [this] { emit done(m_zone); });
    m_title = label("chronoTitle", QString(), this);
    m_title->setAlignment(Qt::AlignCenter);
    m_save = button("editSave", "Save", this, 56);
    connect(m_save, &QPushButton::clicked, this, &ChronoEditor::save);
    QHBoxLayout *header = new QHBoxLayout;
    header->setSpacing(12);
    header->addWidget(cancel);
    header->addWidget(m_title, 1);
    header->addWidget(m_save);

    /* weekday / holiday, on / off */
    m_weekdayTab = button("editTab", "Weekday", this, 52);
    m_holidayTab = button("editTab", "Holiday", this, 52);
    for(QPushButton *b : { m_weekdayTab, m_holidayTab })
        b->setCheckable(true);
    connect(m_weekdayTab, &QPushButton::clicked, this, [this] { showProfile(false); });
    connect(m_holidayTab, &QPushButton::clicked, this, [this] { showProfile(true); });
    m_onOff = button("chronoToggle", QString(), this, 56);
    connect(m_onOff, &QPushButton::clicked, this, [this] {
        m_edit.enabled = !m_edit.enabled;
        changed();
    });
    QHBoxLayout *tabs = new QHBoxLayout;
    tabs->setSpacing(10);
    tabs->addWidget(m_weekdayTab, 1);
    tabs->addWidget(m_holidayTab, 1);
    tabs->addWidget(m_onOff);

    m_warning = label("chronoMessage", QString(), this);
    m_warning->setWordWrap(true);
    setStyleProperty(m_warning, "level", "bad");
    m_warning->hide();

    /* the profile, the slots */
    m_chart = new DayChart(this);
    m_chart->setFixedHeight(240);
    m_chart->setShowValues(true);
    QHBoxLayout *axis = new QHBoxLayout;
    for(const char *h : { "00", "06", "12", "18", "24" })
    {
        axis->addWidget(label("chronoAxis", h, this));
        if(QString(h) != "24")
            axis->addStretch();
    }
    m_asWeekday = label("chronoNone", "Same as the weekday profile: Split adds a first slot", this);
    m_asWeekday->setAlignment(Qt::AlignCenter);
    m_chipsLayout = new QHBoxLayout;
    m_chipsLayout->setSpacing(8);
    for(int i = 0; i < ChronoConfig::MAX_SLOTS; i++)
    {
        QPushButton *chip = button("editChip", QString(), this, 48);
        chip->setCheckable(true);
        connect(chip, &QPushButton::clicked, this, [this, i] { select(i); refresh(); });
        m_chipsLayout->addWidget(chip, 1);
        m_chips.append(chip);
    }

    /* the selected slot */
    QFrame *slotBox = new QFrame(this);
    slotBox->setObjectName("chronoBox");
    m_time = label("editValue", QString(), slotBox);
    m_temp = label("editValue", QString(), slotBox);
    m_timeMinus = stepButton(QString::fromUtf8("−"), "Earlier", slotBox);
    m_timePlus = stepButton("+", "Later", slotBox);
    m_tempMinus = stepButton(QString::fromUtf8("−"), "Lower", slotBox);
    m_tempPlus = stepButton("+", "Higher", slotBox);
    connect(m_timeMinus, &QPushButton::clicked, this, [this] { if(Chrono::stepTime(profile(), m_selected, -1)) changed(); });
    connect(m_timePlus, &QPushButton::clicked, this, [this] { if(Chrono::stepTime(profile(), m_selected, +1)) changed(); });
    connect(m_tempMinus, &QPushButton::clicked, this, [this] { if(Chrono::stepTemp(profile(), m_selected, -1)) changed(); });
    connect(m_tempPlus, &QPushButton::clicked, this, [this] { if(Chrono::stepTemp(profile(), m_selected, +1)) changed(); });
    QGridLayout *g = new QGridLayout(slotBox);
    g->setContentsMargins(16, 14, 16, 14);
    g->setHorizontalSpacing(12);
    g->setVerticalSpacing(8);
    const auto column = [&](int col, const char *title, QLabel *value, QPushButton *minus, QPushButton *plus, const QString &hint) {
        QLabel *t = label("chronoProfileTitle", title, slotBox);
        t->setAlignment(Qt::AlignCenter);
        value->setAlignment(Qt::AlignCenter);
        QHBoxLayout *pm = new QHBoxLayout;
        pm->setSpacing(12);
        pm->addStretch();
        pm->addWidget(minus);
        pm->addWidget(plus);
        pm->addStretch();
        QLabel *h = label("chronoAxis", hint, slotBox);
        h->setAlignment(Qt::AlignCenter);
        g->addWidget(t, 0, col);
        g->addWidget(value, 1, col);
        g->addLayout(pm, 2, col);
        g->addWidget(h, 3, col);
    };
    column(0, "FROM", m_time, m_timeMinus, m_timePlus, "15 min, hold to repeat");
    column(1, "SETPOINT", m_temp, m_tempMinus, m_tempPlus, QString::fromUtf8("0.5°, 5…25"));

    m_prev = button("editButton", QString::fromUtf8("‹ Prev"), this, 64);
    m_next = button("editButton", QString::fromUtf8("Next ›"), this, 64);
    m_split = button("editSplit", "Split", this, 64);
    m_delete = button("editDelete", "Delete", this, 64);
    connect(m_prev, &QPushButton::clicked, this, [this] { select(m_selected - 1); refresh(); });
    connect(m_next, &QPushButton::clicked, this, [this] { select(m_selected + 1); refresh(); });
    connect(m_split, &QPushButton::clicked, this, [this] {
        const int k = Chrono::splitSlot(profile(), m_selected);
        if(k >= 0)
        {
            select(k);
            changed();
        }
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if(m_selected < 0 || m_selected >= profile().size())
            return;
        profile().remove(m_selected);
        select(qMin(m_selected, profile().size() - 1));
        changed();
    });
    QHBoxLayout *ops = new QHBoxLayout;
    ops->setSpacing(10);
    for(QPushButton *b : { m_prev, m_next, m_split, m_delete })
        ops->addWidget(b, 1);

    m_copy = button("editButton", QString(), this, 52);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        /* weekday shown: copy it to holiday; holiday shown: back to "as weekday" */
        m_edit.holiday = m_holiday ? QVector<ChronoSlot>() : m_edit.weekday;
        select(m_holiday ? -1 : m_selected);
        changed();
    });
    m_reset = button("editButton", "Reset to the controller's", this, 52);
    connect(m_reset, &QPushButton::clicked, this, [this] {
        if(m_control->resetProgram(m_zone))
            emit done(m_zone);
    });
    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->addWidget(m_copy);
    bottom->addStretch();
    bottom->addWidget(m_reset);

    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 12, 16, 16);
    page->setSpacing(12);
    page->addLayout(header);
    page->addLayout(tabs);
    page->addWidget(m_warning);
    page->addWidget(m_chart);
    page->addLayout(axis);
    page->addWidget(m_asWeekday);
    page->addLayout(m_chipsLayout);
    page->addWidget(slotBox);
    page->addLayout(ops);
    page->addStretch();
    page->addLayout(bottom);

    connect(m_control, &ZoneControl::chronoChanged, this, [this](int index) {
        if(index == m_zone)
            refresh();
    });
    connect(m_control, &ZoneControl::controllerOnlineChanged, this, &ChronoEditor::refresh);
}

void ChronoEditor::edit(int index)
{
    m_zone = index;
    const ZoneControl::chrono_t &c = m_control->chrono(index);
    m_edit = c.program;
    m_version = c.programVersion;
    m_warned = false;
    m_warning->hide();
    m_title->setText(m_names.value(index));
    m_holidayTab->setText("Holiday " + ChronoView::daysText(m_edit.holidayDays));
    showProfile(false);
}

QVector<ChronoSlot> &ChronoEditor::profile()
{
    return m_holiday ? m_edit.holiday : m_edit.weekday;
}

void ChronoEditor::select(int k)
{
    const int n = profile().size();
    m_selected = n == 0 ? -1 : qBound(0, k, n - 1);
}

void ChronoEditor::showProfile(bool holiday)
{
    m_holiday = holiday;
    select(0);
    refresh();
}

void ChronoEditor::changed()
{
    refresh();
}

void ChronoEditor::save()
{
    /* changed elsewhere since the edit started: say it once */
    if(m_control->chrono(m_zone).programVersion != m_version && !m_warned)
    {
        m_warned = true;
        m_warning->setText("The program was changed on another panel meanwhile: Save again to overwrite it");
        m_warning->show();
        refresh();
        return;
    }
    if(m_control->saveProgram(m_zone, m_edit))
        emit done(m_zone);
}

void ChronoEditor::refresh()
{
    if(m_zone < 0)
        return;
    QVector<ChronoSlot> &list = profile();
    const bool empty = list.isEmpty();
    const bool asWeekday = m_holiday && empty;

    m_weekdayTab->setChecked(!m_holiday);
    m_holidayTab->setChecked(m_holiday);
    m_onOff->setText(m_edit.enabled ? "ON" : "OFF");
    setStyleProperty(m_onOff, "state", m_edit.enabled ? "on" : "off");

    /* the profile alone, as a day of it */
    ChronoConfig day;
    day.weekday = asWeekday ? m_edit.weekday : list;
    m_chart->setTemps(Chrono::dayTemps(QDate(2026, 1, 1), day), !asWeekday && m_edit.enabled);
    if(m_selected >= 0 && !asWeekday)
    {
        const int from = list.at(m_selected).at.msecsSinceStartOfDay() / 60000;
        const int to = m_selected + 1 < list.size() ? list.at(m_selected + 1).at.msecsSinceStartOfDay() / 60000 : 24 * 60;
        m_chart->setSelection(from, to);
    }
    else
        m_chart->setSelection(-1, -1);
    m_asWeekday->setVisible(asWeekday);

    for(int i = 0; i < m_chips.size(); i++)
    {
        m_chips.at(i)->setVisible(i < list.size());
        if(i < list.size())
            m_chips.at(i)->setText(list.at(i).at.toString("HH:mm"));
        m_chips.at(i)->setChecked(i == m_selected);
    }

    const bool has = m_selected >= 0;
    m_time->setText(has ? list.at(m_selected).at.toString("HH:mm") : QString("--:--"));
    m_temp->setText(has ? QString::number(list.at(m_selected).temp, 'f', 1) + QString::fromUtf8("°") : QString("--.-"));
    for(QPushButton *b : { m_timeMinus, m_timePlus, m_tempMinus, m_tempPlus, m_delete })
        b->setEnabled(has);
    m_prev->setEnabled(has && m_selected > 0);
    m_next->setEnabled(has && m_selected + 1 < list.size());
    m_split->setText(empty ? "Add" : "Split");
    m_split->setEnabled(list.size() < ChronoConfig::MAX_SLOTS);
    m_copy->setText(m_holiday ? "Same as weekday" : "Copy to holiday");
    m_copy->setEnabled(m_holiday ? !empty : !m_edit.weekday.isEmpty());

    /* the controller refuses "on" without a weekday slot */
    const bool valid = !(m_edit.enabled && m_edit.weekday.isEmpty());
    const bool canSave = m_control->canChangeChrono(m_zone);
    m_save->setEnabled(canSave && valid);
    m_save->setText(m_warned ? "Save anyway" : "Save");
    m_reset->setEnabled(canSave && m_control->chrono(m_zone).program.edited);
    if(!valid && !m_warned)
    {
        m_warning->setText("Add a weekday slot or turn the chrono off");
        m_warning->show();
    }
    else if(!m_warned)
        m_warning->hide();
}
