#include "mainpage.h"
#include "longpress.h"
#include "modepopup.h"
#include "monoclock.h"
#include "zonecard.h"
#include "zoneview.h"
#include <QDateTime>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include <QScrollArea>
#include <QScroller>
#include <QStyle>
#include <QVBoxLayout>

static QLabel *label(const char *objectName, const QString &text, QWidget *parent)
{
    QLabel *l = new QLabel(text, parent);
    l->setObjectName(objectName);
    return l;
}

MainPage::MainPage(const QVector<sensor_t> &sensors, ZoneControl *control, QWidget *parent) :
    QWidget(parent),
    m_control(control),
    m_lastMs(sensors.size(), 0),
    m_last(sensors.size()),
    m_brokerConnected(false)
{
    setObjectName("mainPage");

    /* header: clock (long press, no hint: diagnostics), house mode,
     * controller and network */
    QWidget *clockBox = new QWidget(this);
    m_clock = label("clockLabel", QString(), clockBox);
    m_date = label("dateLabel", QString(), clockBox);
    QVBoxLayout *cl = new QVBoxLayout(clockBox);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(m_clock);
    cl->addWidget(m_date);
    LongPress *press = new LongPress(clockBox);
    connect(press, &LongPress::longPressed, this, &MainPage::diagnosticsRequested);

    QGridLayout *state = new QGridLayout;
    state->setHorizontalSpacing(8);
    m_controllerDot = label("stateDot", QString(), this);
    m_controllerText = label("stateText", QString(), this);
    m_networkDot = label("stateDot", QString(), this);
    m_networkText = label("stateText", QString(), this);
    for(QLabel *d : { m_controllerDot, m_networkDot })
        d->setFixedSize(12, 12);
    state->addWidget(label("stateName", "Controller", this), 0, 0, Qt::AlignRight);
    state->addWidget(m_controllerDot, 0, 1);
    state->addWidget(m_controllerText, 0, 2);
    state->addWidget(label("stateName", "Network", this), 1, 0, Qt::AlignRight);
    state->addWidget(m_networkDot, 1, 1);
    state->addWidget(m_networkText, 1, 2);

    /* house mode: the button shows it, a press opens the popup */
    m_modeButton = new QToolButton(this);
    m_modeButton->setObjectName("houseMode");
    m_modeButton->setAccessibleName("House mode");
    m_modeButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_modeButton->setIconSize(QSize(40, 40));
    m_modeButton->setFixedSize(124, 104);
    m_modeButton->setFocusPolicy(Qt::NoFocus);

    QHBoxLayout *header = new QHBoxLayout;
    header->addWidget(clockBox);
    header->addStretch();
    header->addWidget(m_modeButton);
    header->addStretch();
    header->addLayout(state);

    m_banner = label("offlineBanner", "Heating controller not reachable: setpoints are read only", this);
    m_banner->setWordWrap(true);

    /* one card per sensor, scrolled by touch when they do not fit */
    QWidget *cardsBox = new QWidget;
    cardsBox->setObjectName("cardsBox");
    QVBoxLayout *cards = new QVBoxLayout(cardsBox);
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(8);
    for(int i = 0; i < sensors.size(); i++)
    {
        const QString zone = QString::fromLatin1(sensors.at(i).zone);
        const QString name = QString::fromUtf8(sensors.at(i).name);
        m_hasZone.append(!zone.isEmpty());
        ZoneCard *card = new ZoneCard(name.isEmpty() ? zone : name, cardsBox);
        connect(card, &ZoneCard::minusClicked, this, [this, i] { m_control->step(i, -1); });
        connect(card, &ZoneCard::plusClicked, this, [this, i] { m_control->step(i, 1); });
        cards->addWidget(card);
        m_cards.append(card);
    }
    cards->addStretch();
    QScrollArea *area = new QScrollArea(this);
    area->setObjectName("cardsArea");
    area->setWidget(cardsBox);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);

    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 12, 16, 16);
    page->setSpacing(12);
    page->addLayout(header);
    page->addWidget(m_banner);
    page->addWidget(area, 1);

    m_modePopup = new ModePopup(control, this);
    connect(m_modeButton, &QToolButton::clicked, m_modePopup, &ModePopup::open);
    connect(m_control, &ZoneControl::houseModeChanged, this, &MainPage::refreshModeButton);

    connect(m_control, &ZoneControl::zoneChanged, this, &MainPage::refreshCard);
    connect(m_control, &ZoneControl::controllerOnlineChanged, this, &MainPage::refresh);
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainPage::refresh);
    timer->start(1000);
    refresh();
}

void MainPage::closePopup()
{
    m_modePopup->hide();
}

void MainPage::onReading(int index, unsigned time, sensor_env_t data)
{
    (void) time;
    if(index < 0 || index >= m_cards.size())
        return;
    m_last[index] = data;
    m_lastMs[index] = MonoClock::nowMs();
    refreshCard(index);
}

void MainPage::onBrokerState(quint8 state)
{
    m_brokerConnected = (state == 2);       /*QMqttClient::Connected*/
    refreshHeader();
}

void MainPage::refresh()
{
    refreshHeader();
    for(int i = 0; i < m_cards.size(); i++)
        refreshCard(i);
}

void MainPage::refreshHeader()
{
    const QDateTime now = QDateTime::currentDateTime();
    m_clock->setText(now.toString("HH:mm"));
    m_date->setText(QLocale(QLocale::English).toString(now.date(), "ddd d MMM yyyy"));

    const bool online = m_control->controllerOnline();
    m_controllerText->setText(online ? "online" : "offline");
    setStyleProperty(m_controllerDot, "on", online ? "true" : "false");
    m_networkText->setText(m_brokerConnected ? "online" : "offline");
    setStyleProperty(m_networkDot, "on", m_brokerConnected ? "true" : "false");
    m_banner->setVisible(!online);
    refreshModeButton();
}

void MainPage::refreshModeButton()
{
    ZoneView::mode_input_t in;
    in.hasMode = m_control->hasHouseMode();
    in.mode = m_control->houseMode();
    in.pending = m_control->modePending();
    in.pendingMode = m_control->pendingMode();
    in.remainingS = m_control->remainingS();
    const ZoneView::mode_view_t v = ZoneView::modeButton(in);
    m_modeButton->setText(v.detail.isEmpty() ? v.text : v.text + "\n" + v.detail);
    if(m_modeButton->property("iconName").toString() != v.icon)
    {
        m_modeButton->setProperty("iconName", v.icon);
        m_modeButton->setIcon(modeIcon(v.icon, 80));
    }
    setStyleProperty(m_modeButton, "level", v.level);
}

void MainPage::refreshCard(int index)
{
    if(index < 0 || index >= m_cards.size())
        return;
    ZoneView::input_t in;
    in.hasZone = m_hasZone.at(index);
    in.heard = m_lastMs.at(index) != 0;
    in.ageS = in.heard ? (MonoClock::nowMs() - m_lastMs.at(index)) / 1000 : 0;
    in.temp = m_last.at(index).temp;
    in.hum = m_last.at(index).hum;
    in.batt = m_last.at(index).batt;
    in.control = m_control->zone(index);
    in.controllerOnline = m_control->controllerOnline();
    in.houseMode = m_control->houseMode();
    m_cards.at(index)->display(ZoneView::make(in), m_control->canChange(index));
}
