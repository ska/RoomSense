#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "common.h"
#include "configfile.h"
#ifdef ENABLE_MQTT
#include "mqtt.h"
#endif

class BleStatus;
class ChronoEditor;
class ChronoPage;
class DiagPage;
class MainPage;
class QStackedWidget;
class QTimer;
class ScreenSaver;
class Weather;
class WatchDog;

/*
 * The panel window: main page (zones), diagnostic page and screen saver in
 * a stack. Every press anywhere restarts the idle time; after it the
 * screen saver comes, and a press on it goes back to the main page (that
 * press is not passed on).
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Mqtt *mqtt, QWidget *parent = nullptr);
    /* Pages; ble, watchdog, weather: nullptr when none */
    void setupPages(QVector<sensor_t> *sensors, const BleStatus *ble = nullptr,
                    const WatchDog *watchdog = nullptr,
                    const screensaver_t &screenSaver = screensaver_t(),
                    const Weather *weather = nullptr);
    /* Tests: a shorter idle time (0 = no screen saver) */
    void setIdleMs(int ms);

public slots:
    void showWindow();
    void showMainPage();
    void showDiagnosticPage();
    void showChronoPage(int zone);
    void showChronoEditor(int zone);
    void showScreenSaver();
    /* A new reading of sensor index, for both pages */
    void onReading(int index, unsigned time, sensor_env_t data);

private:
    Mqtt            *m_mqtt;
    QStackedWidget  *m_stack;
    MainPage        *m_mainPage;
    DiagPage        *m_diagPage;
    ChronoPage      *m_chronoPage;
    ChronoEditor    *m_chronoEditor;
    ScreenSaver     *m_saver;
    QTimer          *m_idleTimer;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
#endif // MAINWINDOW_H
