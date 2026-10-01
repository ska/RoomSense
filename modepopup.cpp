#include "modepopup.h"
#include "zonecard.h"
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

QIcon modeIcon(const QString &name, int size, const QString &color)
{
    QFile file(QString(":/icons/%1.svg").arg(name));
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    if(!color.isEmpty())
        data.replace("#F3F4F5", color.toLatin1());
    QSvgRenderer svg(data);
    QIcon icon;
    for(QIcon::Mode mode : { QIcon::Normal, QIcon::Disabled })
    {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        if(mode == QIcon::Disabled)
            p.setOpacity(0.3);
        svg.render(&p);
        p.end();
        icon.addPixmap(pm, mode);
    }
    return icon;
}

ModePopup::ModePopup(ZoneControl *control, QWidget *parent) :
    QWidget(parent),
    m_control(control)
{
    setObjectName("modeOverlay");
    setAttribute(Qt::WA_StyledBackground);

    m_sheet = new QWidget(this);
    m_sheet->setObjectName("modeSheet");
    m_sheet->setAttribute(Qt::WA_StyledBackground);

    QLabel *title = new QLabel("House", m_sheet);
    title->setObjectName("modeTitle");
    QPushButton *close = new QPushButton("Close", m_sheet);
    close->setObjectName("modeClose");
    close->setFixedHeight(52);
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(title);
    top->addStretch();
    top->addWidget(close);

    auto modeButton = [this](const QString &icon, const QString &name) {
        QToolButton *b = new QToolButton(m_sheet);
        b->setObjectName("modeButton");
        b->setAccessibleName(name);
        b->setIcon(modeIcon(icon, 96));
        b->setIconSize(QSize(72, 72));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setFixedHeight(170);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    m_boost = modeButton("flame", "Boost");
    m_window = modeButton("window", "Windows open");
    m_away = modeButton("plane", "Away");
    connect(m_boost, &QToolButton::clicked, this, [this] { choose(ZoneControl::Boost); });
    connect(m_window, &QToolButton::clicked, this, [this] { choose(ZoneControl::Window); });
    connect(m_away, &QToolButton::clicked, this, [this] { choose(ZoneControl::Away); });
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    buttons->addWidget(m_boost);
    buttons->addWidget(m_window);
    buttons->addWidget(m_away);

    /* setpoint of all the rooms */
    QLabel *allName = new QLabel("All rooms", m_sheet);
    allName->setObjectName("allName");
    m_allValue = new QLabel(m_sheet);
    m_allValue->setObjectName("allValue");
    m_allValue->setAlignment(Qt::AlignCenter);
    m_allMinus = new QPushButton(QString::fromUtf8("−"), m_sheet);
    m_allPlus = new QPushButton("+", m_sheet);
    for(QPushButton *b : { m_allMinus, m_allPlus })
    {
        b->setObjectName("stepButton");
        b->setFixedSize(64, 64);
        b->setFocusPolicy(Qt::NoFocus);
    }
    m_allMinus->setAccessibleName("Lower all the setpoints");
    m_allPlus->setAccessibleName("Raise all the setpoints");
    connect(m_allMinus, &QPushButton::clicked, this, [this] { stepAll(-1); });
    connect(m_allPlus, &QPushButton::clicked, this, [this] { stepAll(1); });
    QHBoxLayout *all = new QHBoxLayout;
    all->setSpacing(12);
    all->addWidget(allName);
    all->addStretch();
    all->addWidget(m_allMinus);
    all->addWidget(m_allValue);
    all->addWidget(m_allPlus);
    m_allValue->setFixedWidth(200);

    m_note = new QLabel(m_sheet);
    m_note->setObjectName("modeNote");
    m_note->setWordWrap(true);

    QVBoxLayout *v = new QVBoxLayout(m_sheet);
    v->setContentsMargins(20, 16, 20, 24);
    v->setSpacing(16);
    v->addLayout(top);
    v->addLayout(all);
    v->addLayout(buttons);
    v->addWidget(m_note);

    /* the sheet at the bottom, the rest of the overlay dims the page */
    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(0, 0, 0, 0);
    page->addStretch();
    page->addWidget(m_sheet);

    /* closed after a while without presses on the sheet */
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(IDLE_CLOSE_MS);
    connect(m_idleTimer, &QTimer::timeout, this, &QWidget::hide);
    m_sheet->installEventFilter(this);
    for(QWidget *w : m_sheet->findChildren<QWidget *>())
        w->installEventFilter(this);

    connect(m_control, &ZoneControl::houseModeChanged, this, &ModePopup::refresh);
    connect(m_control, &ZoneControl::controllerOnlineChanged, this, &ModePopup::refresh);
    connect(m_control, &ZoneControl::zoneChanged, this, [this] {
        if(isVisible())
            refresh();
    });
    parent->installEventFilter(this);
    hide();
}

void ModePopup::open()
{
    setGeometry(parentWidget()->rect());
    refresh();
    show();
    raise();
    m_idleTimer->start();
}

void ModePopup::setIdleCloseMs(int ms)
{
    m_idleTimer->setInterval(ms);
}

void ModePopup::refresh()
{
    /* all the rooms: the range of the setpoints shown */
    double low, high;
    const bool any = m_control->range(low, high);
    if(!any)
        m_allValue->setText("--.-");
    else if(low == high)
        m_allValue->setText(QString::number(low, 'f', 1) + QString::fromUtf8("°"));
    else
        m_allValue->setText(QString::fromUtf8("%1° – %2°").arg(low, 0, 'f', 1).arg(high, 0, 'f', 1));
    m_allMinus->setEnabled(any);
    m_allPlus->setEnabled(any);

    const bool can = m_control->canChangeMode() && !m_control->modePending();
    const ZoneControl::HouseMode mode = m_control->houseMode();
    const bool window = m_control->hasHouseMode() && mode == ZoneControl::Window;
    const bool away = m_control->hasHouseMode() && mode == ZoneControl::Away;
    const bool boost = m_control->hasHouseMode() && mode == ZoneControl::Boost;

    m_window->setText(window ? QString("Windows open\non, tap to end") : QString("Windows open"));
    m_away->setText(away ? QString("Away\non, tap to end") : QString("Away"));
    m_boost->setText(boost ? QString("Boost\non, tap to end") : QString("Boost"));
    setStyleProperty(m_boost, "active", boost ? "true" : "false");
    setStyleProperty(m_window, "active", window ? "true" : "false");
    setStyleProperty(m_away, "active", away ? "true" : "false");
    /* AcquaThermoNet refuses windows open while away */
    m_window->setEnabled(can && !away);
    m_away->setEnabled(can);
    m_boost->setEnabled(can);

    if(!m_control->controllerOnline())
        m_note->setText("Heating controller not reachable: the mode cannot be changed");
    else if(!m_control->hasHouseMode())
        m_note->setText("Waiting for the house mode of the heating controller");
    else if(m_control->modePending())
        m_note->setText(QString::fromUtf8("Sending…"));
    else if(away)
        m_note->setText("Away: every room lowered until it is ended here");
    else if(boost)
        m_note->setText("Boost: every room heated up for a while, then back to its setpoint by itself");
    else if(window)
        m_note->setText("Windows open: every room lowered for a while, then back to its setpoint by itself");
    else
        m_note->setText("For the whole house. Boost and windows open end by themselves, away when it is ended here");
}

void ModePopup::stepAll(int direction)
{
    m_control->stepAll(direction);
    refresh();
}

void ModePopup::choose(ZoneControl::HouseMode mode)
{
    const bool active = m_control->hasHouseMode() && m_control->houseMode() == mode;
    m_control->requestMode(active ? ZoneControl::Normal : mode);
    hide();
}

/* a press outside the sheet closes the popup */
void ModePopup::mousePressEvent(QMouseEvent *event)
{
    if(!m_sheet->geometry().contains(event->pos()))
        hide();
    else
        QWidget::mousePressEvent(event);
}

/* follow the size of the page; a press on the sheet restarts the idle time */
bool ModePopup::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == parentWidget())
    {
        if(event->type() == QEvent::Resize && isVisible())
            setGeometry(parentWidget()->rect());
    }
    else if(event->type() == QEvent::MouseButtonPress && isVisible())
        m_idleTimer->start();
    return QWidget::eventFilter(watched, event);
}
