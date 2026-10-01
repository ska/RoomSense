#include "mainwindow.h"
#ifdef ENABLE_BLE
#include "blesensors.h"
#else
#include "fakebledata.h"
#endif
#ifdef ENABLE_MQTT
#include "mqtt.h"
#endif

#ifdef ENABLE_DBUS
#include "dbusmanager.h"
#include "roomsense_adaptor.h"
#endif

#include "singleinstance.h"
#include "watchdog.h"
#include "common.h"
#include "configfile.h"
#include "weather.h"

#include <QElapsedTimer>
#include <QSocketNotifier>
#include <csignal>

#define BLE_STALL_MS    30000   /*BLE thread without a heartbeat: reboot by the watchdog*/

/*
 * Unix signals (SIGTERM/SIGINT): the handler only writes to a socketpair,
 * the Qt side reads it and quits the event loop, so the normal shutdown
 * (MQTT offline) runs. As AcquaThermoNet; see Qt doc "Calling Qt Functions
 * From Unix Signal Handlers".
 * * */
static int sigFd[2];

static void unixSignalHandler(int)
{
    char a = 1;
    ssize_t r = ::write(sigFd[0], &a, sizeof(a));
    (void) r;
}

static bool setupUnixSignals()
{
    if( ::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) != 0 )
        return false;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = unixSignalHandler;
    sigemptyset(&sa.sa_mask);
    /* One shot: a second signal gets the default action (forced kill) */
    sa.sa_flags = SA_RESTART | SA_RESETHAND;

    if( sigaction(SIGTERM, &sa, nullptr) != 0 )
        return false;
    if( sigaction(SIGINT, &sa, nullptr) != 0 )
        return false;
    return true;
}

int main(int argc, char *argv[])
{
    /* Before QApplication: works without a display */
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version"))
        {
            printf("%s\n", SW_BANNER);
            return 0;
        }
    }

    Logging::install();
    QApplication a(argc, argv);

    QCoreApplication::setApplicationName( SW_NAME );
    QCoreApplication::setApplicationVersion( SW_VER );

    qCInfo(lcApp).noquote() << SW_BANNER;

    /* One instance per target, checked before anything else (D-Bus, BLE,
     * MQTT), as AcquaThermoNet. Must live until exit: the lock lasts as
     * long as this object.
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    SingleInstance instance( SW_NAME );
    if ( !instance.tryLock() )
        return EXIT_ALREADY_RUNNING;

    if( setupUnixSignals() )
    {
        QSocketNotifier *sigNotifier = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &a);
        QObject::connect(sigNotifier, &QSocketNotifier::activated, &a, [sigNotifier] {
            sigNotifier->setEnabled(false);
            char c;
            ssize_t r = ::read(sigFd[1], &c, sizeof(c));
            (void) r;
            qCInfo(lcApp) << "Termination signal received, shutting down";
            QCoreApplication::quit();
        });
    } else {
        qCWarning(lcApp) << "Unix signal handler setup failed";
    }

#ifdef DBUSMANAGER_H
    /* D-Bus SERVICE: optional, the gateway runs without a session bus
     *******************************/
    DbusManager *db = nullptr;
    if (!QDBusConnection::sessionBus().isConnected()) {
        qCWarning(lcApp) << "No D-Bus session bus: show/hide service not available";
    } else {
        qCDebug(lcApp).noquote() << "Start: DbusManager";
        db = new DbusManager(&a);
        new GuiInterfaceAdaptor(db);
        QDBusConnection dbusConnection = QDBusConnection::sessionBus();
        if (!dbusConnection.registerService("org.roomsense.RoomSense"))
            qCWarning(lcApp) << "Error registering service to session bus: " << dbusConnection.lastError();
        if (!dbusConnection.registerObject("/", db))
            qCWarning(lcApp) << "Failed registering the D-Bus object: " << dbusConnection.lastError();
    }
#endif

#ifdef CONFIGFILE_H
    /* Read config file
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    const QDir          appDir(QDir::currentPath());
    ConfigFile::ensureSettings( appDir.filePath("setting.ini"), appDir.filePath("setting.default.ini") );
    ConfigFile          cf( appDir.filePath("setting.ini") );
    QVector <sensor_t>  sensors;
    mqtt_brk_t          mqtt_info;
    memset( &mqtt_info, 0, sizeof(mqtt_brk_t) );

    QString logPath;
    qint64  logMaxBytes;
    int     logFiles;
    cf.ReadLogInfo( logPath, logMaxBytes, logFiles );
    Logging::enableFile( logPath, logMaxBytes, logFiles );

    const screensaver_t screenSaver = cf.ReadScreenSaverInfo();
    Weather *weather = new Weather( cf.ReadWeatherInfo(), &a );
    weather->startPoll();
    cf.ReadSensorsInfo( sensors );
    cf.ReadMqttInfo( &mqtt_info );
#endif

#ifdef BLESENSORS_H
    /* BLE Thread
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    BleSensors *bls;
    bls = new BleSensors(&sensors);
    bls->start();
#endif /*BLESENSORS_H*/

#ifdef FAKEBLEDATA_H
    /* Fake BLE Thread
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    FakeBleData *fkb;

    fkb = new FakeBleData( &sensors, QString::fromLocal8Bit(qgetenv("RS_FAKE")) );
    fkb->start();
#endif /*FAKEBLEDATA_H*/

#if defined (MQTT_H) && defined (CONFIGFILE_H)
    /* MQTT Send Data
     * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
    Mqtt *mq = new Mqtt( &mqtt_info, &sensors, &a );
#endif

#if defined BLESENSORS_H && defined MQTT_H
    QObject::connect( bls, SIGNAL( sensorEnvValueChanged(int, unsigned, sensor_env_t)), mq, SLOT(sensorValueIsChanged(int, unsigned, sensor_env_t)) );
#endif

#if defined FAKEBLEDATA_H && defined MQTT_H
    QObject::connect( fkb, SIGNAL( sensorEnvValueChanged(int, unsigned, sensor_env_t)), mq, SLOT(sensorValueIsChanged(int, unsigned, sensor_env_t)) );
    /* RS_FAKE press=S@T:N: as N presses on the panel */
    QObject::connect( fkb, &FakeBleData::panelPress, mq->zoneControl(), [mq](int index, int steps) {
        qCInfo(lcApp) << "Fake panel press: sensor" << index << "steps" << steps
                      << (mq->zoneControl()->step(index, steps) ? "accepted" : "refused");
    });
    /* RS_FAKE mode=M@T: as chosen in the house mode popup */
    QObject::connect( fkb, &FakeBleData::panelMode, mq->zoneControl(), [mq](const QString &mode) {
        ZoneControl::HouseMode m = mode == "window" ? ZoneControl::Window
                                 : mode == "away" ? ZoneControl::Away
                                 : mode == "boost" ? ZoneControl::Boost : ZoneControl::Normal;
        qCInfo(lcApp).noquote() << "Fake panel house mode" << mode
                                << (mq->zoneControl()->requestMode(m) ? "accepted" : "refused");
    });
#endif


    /*
     * WatchDog: refreshed by the main event loop while the BLE thread is
     * alive; a hang of either reboots the board (the package restart loop
     * only handles a crash)
     * * */
    WatchDog *wdt = new WatchDog(&a);
#ifdef BLESENSORS_H
    wdt->setHealthCheck( [bls] { return bls->heartbeat().isAlive(BLE_STALL_MS); } );
#endif
    wdt->start();

    MainWindow w( mq );
#ifdef BLESENSORS_H
    QObject::connect( bls, SIGNAL( sensorEnvValueChanged(int, unsigned, sensor_env_t)), &w, SLOT(onReading(int, unsigned, sensor_env_t)) );
#endif
#ifdef FAKEBLEDATA_H
    QObject::connect( fkb, SIGNAL( sensorEnvValueChanged(int, unsigned, sensor_env_t)), &w, SLOT(onReading(int, unsigned, sensor_env_t)) );
#endif
#ifdef DBUSMANAGER_H
    if (db)
    {
        QObject::connect(db, SIGNAL(dbusGuiHide()), &w, SLOT(hide()), Qt::QueuedConnection);
        QObject::connect(db, SIGNAL(dbusGuiShow()), &w, SLOT(showWindow()), Qt::QueuedConnection);
    }
#endif

#ifdef BLESENSORS_H
    w.setupPages( &sensors, &bls->status(), wdt, screenSaver, weather );
#endif
#ifdef FAKEBLEDATA_H
    w.setupPages( &sensors, &fkb->status(), wdt, screenSaver, weather );
#endif
    w.showWindow();
    int ret = QApplication::exec();

    /*
     * End: no more readings, then MQTT offline and disconnected. The
     * disconnect needs the event loop: keep processing events (max 5 s).
     * * */
    qCInfo(lcApp) << "Shutting down";
    wdt->setHealthCheck(nullptr);       /* the BLE thread stops on purpose */
#ifdef BLESENSORS_H
    bls->stop();
    bls->wait(5000);
#endif
#ifdef FAKEBLEDATA_H
    fkb->stop();
    fkb->wait(5000);
#endif
    mq->shutdown();
    QElapsedTimer flushTimer;
    flushTimer.start();
    while( flushTimer.elapsed() < 5000 && !mq->isDisconnected() )
    {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    /* Disarm only now: the shutdown above keeps refreshing it */
    wdt->stop();
    qCInfo(lcApp) << "End Application";

    return ret;
}
