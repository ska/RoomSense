#include "screensaver.h"
#include "modepopup.h"
#include "monoclock.h"
#include "saverview.h"
#include "weather.h"
#include "zonecard.h"
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVBoxLayout>

static QLabel *label(const char *objectName, QWidget *parent)
{
    QLabel *l = new QLabel(parent);
    l->setObjectName(objectName);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

ScreenSaver::ScreenSaver(const QVector<sensor_t> &sensors, ZoneControl *control,
                         const screensaver_t &config, const Weather *weather, QWidget *parent) :
    QWidget(parent),
    m_control(control),
    m_config(config),
    m_lastMs(sensors.size(), 0),
    m_last(sensors.size()),
    m_brokerConnected(false),
    m_weatherSource(weather),
    m_reply(nullptr),
    m_failed(false)
{
    setObjectName("screenSaver");
    setAttribute(Qt::WA_StyledBackground);
    for(const sensor_t &s : sensors)
    {
        const QString name = QString::fromUtf8(s.name);
        const QString zone = QString::fromLatin1(s.zone);
        QString shown = name.isEmpty() ? zone : name;
        if(!shown.isEmpty())
            shown[0] = shown[0].toUpper();
        m_names.append(shown);
        m_hasZone.append(!zone.isEmpty());
    }

    m_clock = label("saverClock", this);
    m_date = label("saverDate", this);
    /* weather: icon, temperature and the details under it */
    m_weather = new QWidget(this);
    m_weatherIcon = new QLabel(m_weather);
    m_weatherTemp = label("weatherTemp", m_weather);
    m_weatherDetails = label("weatherDetails", m_weather);
    QHBoxLayout *wtop = new QHBoxLayout;
    wtop->setSpacing(12);
    wtop->addStretch();
    wtop->addWidget(m_weatherIcon);
    wtop->addWidget(m_weatherTemp);
    wtop->addStretch();
    QVBoxLayout *wl = new QVBoxLayout(m_weather);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(2);
    wl->addLayout(wtop);
    wl->addWidget(m_weatherDetails);
    m_weather->hide();

    m_image = label("saverImage", this);
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_image->setMinimumSize(1, 1);
    m_image->setVisible(!m_config.webcamUrl.isEmpty());

    QVBoxLayout *page = new QVBoxLayout(this);
    page->setContentsMargins(16, 40, 16, 40);
    page->setSpacing(16);
    page->addWidget(m_clock);
    page->addWidget(m_date);
    page->addWidget(m_weather);
    page->addWidget(m_image, 1);
    if(m_config.webcamUrl.isEmpty())
        page->addStretch(1);
    m_summary = new QVBoxLayout;
    m_summary->setSpacing(6);
    page->addLayout(m_summary);
    m_heating = new QWidget(this);
    page->addWidget(m_heating);
    m_flame = modeIcon("flame", 64, "#F28C28");

    m_net = new QNetworkAccessManager(this);
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &ScreenSaver::refresh);
    m_webcamTimer = new QTimer(this);
    m_webcamTimer->setInterval(m_config.webcamRefreshS * 1000);
    connect(m_webcamTimer, &QTimer::timeout, this, &ScreenSaver::download);
    m_abortTimer = new QTimer(this);
    m_abortTimer->setSingleShot(true);
    m_abortTimer->setInterval(DOWNLOAD_TIMEOUT_MS);
    connect(m_abortTimer, &QTimer::timeout, this, [this] {
        if(m_reply)
            m_reply->abort();               /* finished with OperationCanceledError */
    });

    connect(m_control, &ZoneControl::zoneChanged, this, [this] {
        if(isVisible())
            refresh();
    });
    connect(m_control, &ZoneControl::houseModeChanged, this, [this] {
        if(isVisible())
            refresh();
    });
    if(m_weatherSource)
        connect(m_weatherSource, &Weather::newWeather, this, [this] {
            if(isVisible())
                refresh();
        });
}

void ScreenSaver::onReading(int index, unsigned time, sensor_env_t data)
{
    (void) time;
    if(index < 0 || index >= m_last.size())
        return;
    m_last[index] = data;
    m_lastMs[index] = MonoClock::nowMs();
}

void ScreenSaver::onBrokerState(quint8 state)
{
    m_brokerConnected = (state == 2);       /*QMqttClient::Connected*/
}

void ScreenSaver::refresh()
{
    const QDateTime now = QDateTime::currentDateTime();
    m_clock->setText(now.toString("HH:mm"));
    m_date->setText(QLocale(QLocale::English).toString(now.date(), "dddd d MMMM yyyy"));

    /* weather, hidden without (recent) data */
    SaverView::weather_view_t wv;
    if(m_weatherSource && m_weatherSource->lastOkMs() != 0)
        wv = SaverView::weather(m_weatherSource->last(), (MonoClock::nowMs() - m_weatherSource->lastOkMs()) / 1000);
    m_weather->setVisible(wv.shown);
    if(wv.shown)
    {
        m_weatherTemp->setText(wv.temp);
        m_weatherDetails->setText(wv.details);
        if(wv.icon != m_weatherIconShown)
        {
            m_weatherIconShown = wv.icon;
            m_weatherIcon->setPixmap(wv.icon.isEmpty() ? QPixmap() : modeIcon(wv.icon, 128).pixmap(64, 64));
        }
        m_weatherIcon->setVisible(!wv.icon.isEmpty());
    }

    SaverView::input_t in;
    in.brokerConnected = m_brokerConnected;
    in.controllerOnline = m_control->controllerOnline();
    for(int i = 0; i < m_names.size(); i++)
    {
        SaverView::zone_t z;
        z.name = m_names.at(i);
        z.hasZone = m_hasZone.at(i);
        const qint64 ageS = (MonoClock::nowMs() - m_lastMs.at(i)) / 1000;
        if(m_lastMs.at(i) == 0)
            z.level = ZoneView::Never;
        else if(ageS > ZoneView::LOST_S)
            z.level = ZoneView::Lost;
        else if(ageS > ZoneView::STALE_S)
            z.level = ZoneView::Stale;
        else
            z.level = ZoneView::Ok;
        z.batt = m_last.at(i).batt;
        if(i < m_control->count())
            z.heat = m_control->zone(i).hasHeat && m_control->zone(i).heat;
        in.zones.append(z);
    }
    ZoneView::mode_input_t mi;
    mi.hasMode = m_control->hasHouseMode();
    mi.mode = m_control->houseMode();
    mi.remainingS = m_control->remainingS();
    in.mode = ZoneView::modeButton(mi);

    /* rooms heating: one chip each (flame and name), centered rows of
     * three; built again only when the rooms change */
    const QStringList heating = SaverView::heating(in);
    if(heating != m_heatingShown)
    {
        m_heatingShown = heating;
        delete m_heating->layout();
        qDeleteAll(m_heating->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
        QVBoxLayout *rows = new QVBoxLayout(m_heating);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(10);
        for(int i = 0; i < heating.size(); i++)
        {
            if(i % 3 == 0)
            {
                QHBoxLayout *row = new QHBoxLayout;
                row->setSpacing(10);
                row->addStretch();
                rows->addLayout(row);
            }
            QFrame *chip = new QFrame(m_heating);
            chip->setObjectName("heatChip");
            QLabel *icon = new QLabel(chip);
            icon->setPixmap(m_flame.pixmap(28, 28));
            QLabel *name = new QLabel(heating.at(i), chip);
            name->setObjectName("heatName");
            QHBoxLayout *h = new QHBoxLayout(chip);
            h->setContentsMargins(10, 6, 14, 6);
            h->setSpacing(6);
            h->addWidget(icon);
            h->addWidget(name);
            QHBoxLayout *row = qobject_cast<QHBoxLayout *>(rows->itemAt(rows->count() - 1)->layout());
            row->addWidget(chip);
            if(i % 3 == 2 || i == heating.size() - 1)
                row->addStretch();
        }
    }

    /* one label per line, reused */
    static const char *levels[] = { "ok", "info", "alarm" };
    const QVector<SaverView::line_t> lines = SaverView::summary(in);
    while(m_summary->count() < lines.size())
    {
        QLabel *l = label("saverLine", this);
        l->setWordWrap(true);
        m_summary->addWidget(l);
    }
    for(int i = 0; i < m_summary->count(); i++)
    {
        QLabel *l = qobject_cast<QLabel *>(m_summary->itemAt(i)->widget());
        l->setVisible(i < lines.size());
        if(i < lines.size())
        {
            l->setText(lines.at(i).text);
            setStyleProperty(l, "level", levels[lines.at(i).level]);
        }
    }
}

void ScreenSaver::mousePressEvent(QMouseEvent *event)
{
    (void) event;
    emit wakeRequested();
}

void ScreenSaver::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
    m_clockTimer->start(1000);
    if(!m_config.webcamUrl.isEmpty())
    {
        download();
        m_webcamTimer->start();
    }
}

void ScreenSaver::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_clockTimer->stop();
    m_webcamTimer->stop();
}

void ScreenSaver::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    showImage();
}

/**
 * @brief ScreenSaver::download
 * One request at a time; aborted after DOWNLOAD_TIMEOUT_MS (Qt 5.13 has
 * no transfer timeout of its own)
 */
void ScreenSaver::download()
{
    if(m_reply || m_config.webcamUrl.isEmpty())
        return;
    QNetworkRequest request{QUrl(m_config.webcamUrl)};
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    m_reply = m_net->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this] { downloaded(m_reply); });
    m_abortTimer->start();
}

void ScreenSaver::downloaded(QNetworkReply *reply)
{
    m_abortTimer->stop();
    m_reply = nullptr;
    reply->deleteLater();

    QPixmap pm;
    QString error;
    if(reply->error() != QNetworkReply::NoError)
        error = reply->error() == QNetworkReply::OperationCanceledError ? QString("timeout") : reply->errorString();
    else if(!pm.loadFromData(reply->readAll()))
        error = "not an image";

    if(!error.isEmpty())
    {
        /* logged once until it works again; the last image stays */
        if(!m_failed)
            qCWarning(lcApp).noquote() << "Webcam image not loaded:" << error;
        m_failed = true;
        showImage();
        return;
    }
    if(m_failed)
        qCInfo(lcApp) << "Webcam image loaded again";
    m_failed = false;
    m_pixmap = pm;
    showImage();
}

void ScreenSaver::showImage()
{
    if(m_pixmap.isNull())
    {
        m_image->setPixmap(QPixmap());
        m_image->setText(m_failed ? "Webcam not available" : QString());
        return;
    }
    m_image->setPixmap(m_pixmap.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
