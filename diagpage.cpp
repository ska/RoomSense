#include "diagpage.h"
#include "blestatus.h"
#include "diagview.h"
#include "monoclock.h"
#include "mqtt.h"
#include "watchdog.h"
#include "weather.h"
#include "zonecard.h"
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

static const char *levels[] = { "ok", "warn", "bad", "muted" };
static const char *sensorLevels[] = { "ok", "stale", "lost", "never" };

static QLabel *label(const char *objectName, const QString &text, QWidget *parent)
{
    QLabel *l = new QLabel(text, parent);
    l->setObjectName(objectName);
    return l;
}

DiagPage::DiagPage(const QVector<sensor_t> *sensors, Mqtt *mqtt, const BleStatus *ble,
                   const WatchDog *watchdog, const Weather *weather, QWidget *parent) :
    QWidget(parent),
    m_sensors(sensors),
    m_mqtt(mqtt),
    m_ble(ble),
    m_watchdog(watchdog),
    m_weather(weather),
    m_startMs(MonoClock::nowMs()),
    m_lastMs(sensors->size(), 0),
    m_last(sensors->size()),
    m_logShown(quint64(-1))
{
    setObjectName("diagPage");

    /* header: back, title, version */
    QPushButton *back = new QPushButton(QString::fromUtf8("‹  Back"), this);
    back->setObjectName("backButton");
    back->setFixedHeight(52);
    back->setFocusPolicy(Qt::NoFocus);
    connect(back, &QPushButton::clicked, this, &DiagPage::backRequested);
    QHBoxLayout *header = new QHBoxLayout;
    header->addWidget(back);
    header->addStretch();
    header->addWidget(label("diagTitle", "Diagnostics", this));
    header->addStretch();
    header->addWidget(label("diagVersion", SW_NAME " v" SW_VER, this));

    /* System tab: gateway state, one line each */
    QWidget *systemTab = new QWidget;
    QFrame *systemBox = new QFrame(systemTab);
    systemBox->setObjectName("diagBox");
    QGridLayout *grid = new QGridLayout(systemBox);
    grid->setContentsMargins(18, 14, 18, 14);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(16);
    const QVector<DiagView::row_t> rows = DiagView::system(DiagView::system_t());
    for(int i = 0; i < rows.size(); i++)
    {
        QLabel *dot = label("diagDot", QString(), systemBox);
        dot->setFixedSize(10, 10);
        QLabel *value = label("diagValue", QString(), systemBox);
        grid->addWidget(label("diagLabel", rows.at(i).label, systemBox), i, 0);
        grid->addWidget(dot, i, 1);
        grid->addWidget(value, i, 2);
        m_dots.append(dot);
        m_values.append(value);
    }
    grid->setColumnStretch(2, 1);
    QVBoxLayout *systemPage = new QVBoxLayout(systemTab);
    systemPage->setContentsMargins(0, 12, 0, 0);
    systemPage->addWidget(systemBox);
    systemPage->addStretch();

    /* Sensors tab: one card per sensor, scrolled by touch when they do not fit */
    QWidget *cardsBox = new QWidget;
    cardsBox->setObjectName("cardsBox");
    QVBoxLayout *cards = new QVBoxLayout(cardsBox);
    cards->setContentsMargins(0, 12, 0, 0);
    cards->setSpacing(8);
    for(const sensor_t &s : *sensors)
    {
        const QString name = QString::fromUtf8(s.name);
        sensor_card_t c;
        c.card = new QFrame(cardsBox);
        c.card->setObjectName("sensorCard");
        QHBoxLayout *top = new QHBoxLayout;
        top->addWidget(label("sensorName", name.isEmpty() ? QString::fromLatin1(s.zone) : name, c.card));
        top->addStretch();
        top->addWidget(label("sensorMac", QString::fromLatin1(s.smac), c.card));

        QGridLayout *values = new QGridLayout;
        values->setHorizontalSpacing(8);
        values->setVerticalSpacing(0);
        const char *captions[] = { "RSSI", "Last reading", "Sent", "Filtered" };
        QLabel **fields[] = { &c.rssi, &c.read, &c.sent, &c.filtered };
        for(int col = 0; col < 4; col++)
        {
            values->addWidget(label("sensorCaption", captions[col], c.card), 0, col);
            *fields[col] = label("sensorValue", QString(), c.card);
            values->addWidget(*fields[col], 1, col);
            values->setColumnStretch(col, 1);
        }

        QVBoxLayout *v = new QVBoxLayout(c.card);
        v->setContentsMargins(16, 10, 16, 10);
        v->setSpacing(6);
        v->addLayout(top);
        v->addLayout(values);
        cards->addWidget(c.card);
        m_cards.append(c);
    }
    cards->addStretch();
    QScrollArea *area = new QScrollArea;
    area->setObjectName("cardsArea");
    area->setWidget(cardsBox);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);

    /* Log tab: the last messages, newest first */
    QWidget *logTab = new QWidget;
    m_log = new QPlainTextEdit(logTab);
    m_log->setObjectName("logBox");
    m_log->setReadOnly(true);
    m_log->setFocusPolicy(Qt::NoFocus);
    m_log->setTextInteractionFlags(Qt::NoTextInteraction);
    m_log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(m_log->viewport(), QScroller::LeftMouseButtonGesture);
    QVBoxLayout *logPage = new QVBoxLayout(logTab);
    logPage->setContentsMargins(0, 12, 0, 0);
    logPage->addWidget(m_log);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("diagTabs");
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setExpanding(true);
    m_tabs->tabBar()->setFocusPolicy(Qt::NoFocus);
    m_tabs->addTab(systemTab, "System");
    m_tabs->addTab(area, "Sensors");
    m_tabs->addTab(logTab, "Log");
    connect(m_tabs, &QTabWidget::currentChanged, this, &DiagPage::refresh);

    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 16, 16, 16);
    page->setSpacing(12);
    page->addLayout(header);
    page->addWidget(m_tabs, 1);

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if(isVisible())
            refresh();
    });
    timer->start(1000);
}

void DiagPage::onReading(int index, unsigned time, sensor_env_t data)
{
    (void) time;
    if(index < 0 || index >= m_cards.size())
        return;
    m_last[index] = data;
    m_lastMs[index] = MonoClock::nowMs();
}

void DiagPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void DiagPage::refresh()
{
    const qint64 nowMs = MonoClock::nowMs();

    /* gateway */
    DiagView::system_t in;
    in.brokerState = m_mqtt->clientState();
    in.brokerHost = m_mqtt->brokerHost();
    in.brokerPort = m_mqtt->brokerPort();
    in.clientId = m_mqtt->clientId();
    in.controllerOnline = m_mqtt->zoneControl()->controllerOnline();
    if(m_ble)
    {
        in.bleState = m_ble->state();
        in.bleDevice = m_ble->device();
        if(m_ble->lastAdvertisementMs() != 0)
            in.lastAdvAgeS = (nowMs - m_ble->lastAdvertisementMs()) / 1000;
    }
    if(m_watchdog)
    {
        in.watchdogActive = m_watchdog->isActive();
        in.watchdogTimeoutS = m_watchdog->timeoutS();
    }
    in.uptimeS = (nowMs - m_startMs) / 1000;
    const QString log = Logging::filePath();
    if(!log.isEmpty())
    {
        const QFileInfo fi(log);
        in.logFile = QDir::current().relativeFilePath(fi.absoluteFilePath());
        in.logBytes = fi.exists() ? fi.size() : -1;
    }
    if(m_weather)
    {
        in.weatherEnabled = m_weather->enabled();
        in.weatherSource = m_weather->source();
        in.weatherError = m_weather->lastError();
        if(m_weather->lastOkMs() != 0)
            in.weatherAgeS = (nowMs - m_weather->lastOkMs()) / 1000;
    }
    const QVector<DiagView::row_t> rows = DiagView::system(in);
    for(int i = 0; i < rows.size() && i < m_values.size(); i++)
    {
        m_values.at(i)->setText(rows.at(i).value);
        setStyleProperty(m_dots.at(i), "level", levels[rows.at(i).level]);
    }

    /* sensors */
    const unsigned now = unsigned(QDateTime::currentSecsSinceEpoch());
    for(int i = 0; i < m_cards.size() && i < m_sensors->size(); i++)
    {
        const sensor_t &s = m_sensors->at(i);
        DiagView::sensor_in_t si;
        si.heard = m_lastMs.at(i) != 0;
        si.ageS = si.heard ? (nowMs - m_lastMs.at(i)) / 1000 : 0;
        si.readTime = m_last.at(i).ldt;
        si.rssi = m_last.at(i).rssi;
        si.sent = i < m_mqtt->statsSentData().size() ? m_mqtt->statsSentData().at(i) : 0;
        si.sentTime = s.ldst;
        si.filtered = i < m_mqtt->statsFiltered().size() ? m_mqtt->statsFiltered().at(i) : 0;
        si.now = now;
        const DiagView::sensor_view_t v = DiagView::sensor(si);

        const sensor_card_t &c = m_cards.at(i);
        c.rssi->setText(v.rssi);
        c.read->setText(v.read);
        c.sent->setText(v.sent);
        c.filtered->setText(v.filtered);
        setStyleProperty(c.card, "level", sensorLevels[v.level]);
        setStyleProperty(c.read, "level", sensorLevels[v.level]);
        setStyleProperty(c.filtered, "dropped", si.filtered ? "true" : "false");
    }

    /* log: only when a new message arrived */
    const quint64 count = Logging::recentCount();
    if(count != m_logShown)
    {
        m_logShown = count;
        const QStringList lines = Logging::recent();
        m_log->setPlainText(lines.isEmpty() ? QString("No messages since the start") : lines.join('\n'));
    }
}
