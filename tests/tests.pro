# Unit tests (QtTest), no hardware and no broker needed (the MQTT tests
# run an in-process broker, fakebroker.h). Built by the top level project,
# after ThirdParty (QtMqtt):
#   qmake RoomSense.pro CONFIG+=tests && make && ./tests/tests

QT       += core gui widgets dbus network svg testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle
TARGET    = tests

SRC = $$PWD/..
INCLUDEPATH += $$SRC
DEFINES += ENABLE_MQTT DESKTOP=0 DISPLAY=1 DEVICE=DESKTOP
include($$SRC/ThirdParty/thirdparty.pri)
include($$SRC/version.pri)

SOURCES += \
    main.cpp \
    tst_bleparse.cpp \
    tst_chrono.cpp \
    tst_chronoeditor.cpp \
    tst_chronopage.cpp \
    tst_config.cpp \
    tst_fakescenario.cpp \
    tst_logging.cpp \
    tst_mqtt.cpp \
    tst_singleinstance.cpp \
    tst_watchdog.cpp \
    tst_zonecontrol.cpp \
    tst_zoneview.cpp \
    tst_diagview.cpp \
    tst_diagpage.cpp \
    tst_mainpage.cpp \
    tst_mainwindow.cpp \
    tst_screensaver.cpp \
    tst_weather.cpp \
    $$SRC/bleparse.cpp \
    $$SRC/chrono.cpp \
    $$SRC/chronoeditor.cpp \
    $$SRC/chronopage.cpp \
    $$SRC/chronoview.cpp \
    $$SRC/daychart.cpp \
    $$SRC/configfile.cpp \
    $$SRC/diagpage.cpp \
    $$SRC/diagview.cpp \
    $$SRC/fakescenario.cpp \
    $$SRC/logging.cpp \
    $$SRC/monoclock.cpp \
    $$SRC/mqtt.cpp \
    $$SRC/saverview.cpp \
    $$SRC/screensaver.cpp \
    $$SRC/zonecontrol.cpp \
    $$SRC/zoneview.cpp \
    $$SRC/zonecard.cpp \
    $$SRC/longpress.cpp \
    $$SRC/mainpage.cpp \
    $$SRC/modepopup.cpp \
    $$SRC/mainwindow.cpp \
    $$SRC/singleinstance.cpp \
    $$SRC/watchdog.cpp \
    $$SRC/weather.cpp

HEADERS += \
    fakebroker.h \
    testutil.h \
    $$SRC/chronoeditor.h \
    $$SRC/chronopage.h \
    $$SRC/daychart.h \
    $$SRC/diagpage.h \
    $$SRC/mqtt.h \
    $$SRC/saverview.h \
    $$SRC/screensaver.h \
    $$SRC/zonecontrol.h \
    $$SRC/zonecard.h \
    $$SRC/longpress.h \
    $$SRC/mainpage.h \
    $$SRC/modepopup.h \
    $$SRC/mainwindow.h \
    $$SRC/watchdog.h \
    $$SRC/weather.h

RESOURCES = $$SRC/stylesheet.qrc
