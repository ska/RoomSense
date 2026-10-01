#include "mainwindow.h"
#include "diagpage.h"
#include "mainpage.h"
#include "screensaver.h"
#include <QStackedWidget>
#include <QTimer>

MainWindow::MainWindow(Mqtt *mqtt, QWidget *parent)
    : QMainWindow(parent)
    , m_mqtt(mqtt)
{
    QScreen *screen = QApplication::screens().at(0);
    int screenWidth = screen->availableSize().width();
    int screenHeight = screen->availableSize().height();

    qCDebug(lcApp) << "Screen, W: " << screenWidth << "H: " << screenHeight;
    m_mainPage = nullptr;
    m_diagPage = nullptr;
    m_saver = nullptr;
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, &MainWindow::showScreenSaver);

    /*
     * styleSheet
     */
    QFile file;
    file.setFileName(":/qss/default.qss");
    file.open(QFile::ReadOnly);
    QString styleSheet = QString::fromLatin1(file.readAll());
    qApp->setStyleSheet(styleSheet);

    setWindowTitle(SW_NAME);
    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);
}

void MainWindow::setupPages(QVector<sensor_t> *sensors, const BleStatus *ble, const WatchDog *watchdog,
                            const screensaver_t &screenSaver, const Weather *weather)
{
    m_mainPage = new MainPage(*sensors, m_mqtt->zoneControl(), m_stack);
    m_stack->addWidget(m_mainPage);
    connect( m_mainPage, &MainPage::diagnosticsRequested, this, &MainWindow::showDiagnosticPage );
    connect( m_mqtt, SIGNAL( clientStateChanged(quint8)), m_mainPage, SLOT(onBrokerState(quint8)) );

    m_diagPage = new DiagPage(sensors, m_mqtt, ble, watchdog, weather, m_stack);
    m_stack->addWidget(m_diagPage);
    connect( m_diagPage, &DiagPage::backRequested, this, &MainWindow::showMainPage );

    m_saver = new ScreenSaver(*sensors, m_mqtt->zoneControl(), screenSaver, weather, m_stack);
    m_stack->addWidget(m_saver);
    connect( m_saver, &ScreenSaver::wakeRequested, this, &MainWindow::showMainPage );
    connect( m_mqtt, SIGNAL( clientStateChanged(quint8)), m_saver, SLOT(onBrokerState(quint8)) );

    /* any press, on any widget of the application, is activity */
    qApp->installEventFilter(this);
    setIdleMs(screenSaver.idleS * 1000);
    showMainPage();
}

void MainWindow::setIdleMs(int ms)
{
    m_idleTimer->setInterval(ms);
    if(ms > 0)
        m_idleTimer->start();
    else
        m_idleTimer->stop();
}

/**
 * @brief MainWindow::eventFilter
 * Presses restart the idle time; on the screen saver a press only wakes
 * up the main page (consumed: it must not change a setpoint)
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch(event->type())
    {
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
    case QEvent::KeyPress:
        if(m_idleTimer->interval() > 0)
            m_idleTimer->start();
        if(m_saver && m_stack->currentWidget() == m_saver)
        {
            showMainPage();
            return true;
        }
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showWindow()
{
#if DEVICE == DISPLAY
    setWindowFlags( this->windowFlags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint );
    showMaximized();
#elif DEVICE == DESKTOP
    setGeometry( 500, 100, WIN_WIDTH, WIN_HEIGHT );
    show();
#endif
}

void MainWindow::showMainPage()
{
    if( m_mainPage )
        m_stack->setCurrentWidget(m_mainPage);
}

void MainWindow::showScreenSaver()
{
    if(!m_saver)
        return;
    m_mainPage->closePopup();
    m_stack->setCurrentWidget(m_saver);
}

void MainWindow::showDiagnosticPage()
{
    if( m_diagPage )
        m_stack->setCurrentWidget(m_diagPage);
}

void MainWindow::onReading(int index, unsigned time, sensor_env_t data)
{
    if( m_mainPage )
        m_mainPage->onReading(index, time, data);
    if( m_diagPage )
        m_diagPage->onReading(index, time, data);
    if( m_saver )
        m_saver->onReading(index, time, data);
}
