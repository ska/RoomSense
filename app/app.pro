# RoomSense application. Sources are in the repository root;
# built through the top level RoomSense.pro (ThirdParty first).

QT       += core gui dbus network widgets svg
CONFIG   += c++17
TARGET    = RoomSense

DEFINES += ENABLE_MQTT
DEFINES += ENABLE_DBUS

DEFINES += DESKTOP=0
DEFINES += DISPLAY=1

message("Build Arch: " $${QT_ARCH})

INCLUDEPATH += $$RS_SRC
include($$RS_SRC/ThirdParty/thirdparty.pri)
include($$RS_SRC/version.pri)

# Yocto SDK mkspec (linux-oe-g++): -D_FORTIFY_SOURCE=2 is in the compiler
# command, the optimization comes from OE_QMAKE_CFLAGS (set by the SDK
# environment-setup script). A Qt Creator kit without it builds debug at
# -O0, and glibc warns in every file that FORTIFY needs optimization: no
# FORTIFY in those debug builds (it does nothing without optimization).
CONFIG(debug, debug|release):contains(QMAKE_CXX, .*_FORTIFY_SOURCE.*):!contains(QMAKE_CXXFLAGS, -O.*) {
    QMAKE_CFLAGS   += -U_FORTIFY_SOURCE
    QMAKE_CXXFLAGS += -U_FORTIFY_SOURCE
}

####################################
## Desktop x64
####################################
equals(QT_ARCH, "x86_64") {
    DEFINES += DEVICE=DESKTOP
    INST_PATH = /tmp

    message(Compile for DESKTOP)
}

####################################
## i5/i7 Arm32
####################################
equals(QT_ARCH, "arm") {
    DEFINES += DEVICE=DISPLAY
    INST_PATH = /mnt/data/hmi/RoomSense

    message(Compile for ARM32)
}

equals(RS_BLE, true) {
    message(Enable BLE)
    DEFINES += ENABLE_BLE
    SOURCES += $$RS_SRC/blesensors.cpp
    HEADERS += $$RS_SRC/blesensors.h
} else {
    message(NO Enable BLE)
    SOURCES += $$RS_SRC/fakebledata.cpp $$RS_SRC/fakescenario.cpp
    HEADERS += $$RS_SRC/fakebledata.h $$RS_SRC/fakescenario.h
}

SOURCES += \
    $$RS_SRC/bleparse.cpp \
    $$RS_SRC/configfile.cpp \
    $$RS_SRC/diagpage.cpp \
    $$RS_SRC/diagview.cpp \
    $$RS_SRC/logging.cpp \
    $$RS_SRC/main.cpp \
    $$RS_SRC/longpress.cpp \
    $$RS_SRC/mainpage.cpp \
    $$RS_SRC/modepopup.cpp \
    $$RS_SRC/mainwindow.cpp \
    $$RS_SRC/monoclock.cpp \
    $$RS_SRC/mqtt.cpp \
    $$RS_SRC/saverview.cpp \
    $$RS_SRC/screensaver.cpp \
    $$RS_SRC/zonecard.cpp \
    $$RS_SRC/zonecontrol.cpp \
    $$RS_SRC/zoneview.cpp \
    $$RS_SRC/dbusmanager.cpp \
    $$RS_SRC/singleinstance.cpp \
    $$RS_SRC/watchdog.cpp \
    $$RS_SRC/weather.cpp

HEADERS += \
    $$RS_SRC/bleparse.h \
    $$RS_SRC/blestatus.h \
    $$RS_SRC/configfile.h \
    $$RS_SRC/diagpage.h \
    $$RS_SRC/diagview.h \
    $$RS_SRC/logging.h \
    $$RS_SRC/longpress.h \
    $$RS_SRC/mainpage.h \
    $$RS_SRC/modepopup.h \
    $$RS_SRC/mainwindow.h \
    $$RS_SRC/monoclock.h \
    $$RS_SRC/common.h \
    $$RS_SRC/mqtt.h \
    $$RS_SRC/saverview.h \
    $$RS_SRC/screensaver.h \
    $$RS_SRC/zonecard.h \
    $$RS_SRC/zonecontrol.h \
    $$RS_SRC/zoneview.h \
    $$RS_SRC/dbusmanager.h \
    $$RS_SRC/singleinstance.h \
    $$RS_SRC/heartbeat.h \
    $$RS_SRC/watchdog.h \
    $$RS_SRC/weather.h

RESOURCES = $$RS_SRC/stylesheet.qrc

DBUS_ADAPTORS += $$RS_SRC/roomsense.xml


message("Install PATH: " $${INST_PATH})

# Default rules for deployment.
target.path = $$INST_PATH/
!isEmpty(target.path): INSTALLS += target
DESTDIR = $$RS_BUILD/bin_$${QT_ARCH}

##############################################################
# Default config file
##############################################################
##  local build dir
QMAKE_POST_LINK += $$quote($(COPY_FILE) $${RS_SRC}/setting.default.ini $$DESTDIR ;)
## Install to the target as a template: the device setting.ini is never overwritten
conf_file.path = $$INST_PATH/
conf_file.files += $${RS_SRC}/setting.default.ini
INSTALLS += conf_file

##############################################################
# hciconfig (BlueZ tool built by ThirdParty), next to the binary
##############################################################
!isEmpty(RS_HCICONFIG) {
    QMAKE_POST_LINK += $$quote($(COPY_FILE) $$RS_HCICONFIG $$DESTDIR ;)
    hciconfig.path = $$INST_PATH/
    hciconfig.files += $$RS_HCICONFIG
    INSTALLS += hciconfig
}

##############################################################
# Deployable package (tools/package/README.md), as AcquaThermoNet
# Built after every link of an ARM build; desktop builds only with
# CONFIG+=package, ARM builds can skip it with CONFIG+=nopackage.
# Output: <build dir>/dist/RoomSense_Package_<arch>_<version>.zip
##############################################################
PKG_ARCH = $${QT_ARCH}
equals(QT_ARCH, "arm"):     PKG_ARCH = Arm32
equals(QT_ARCH, "arm64"):   PKG_ARCH = Arm64
if(equals(QT_ARCH, "arm")|equals(QT_ARCH, "arm64")|CONFIG(package)):!CONFIG(nopackage) {
    # $(STRIP) / $(CXX): the toolchain of this Makefile (the strip is searched
    # next to the compiler when the SDK environment is not loaded, Qt Creator)
    QMAKE_POST_LINK += $$quote($${RS_SRC}/tools/package/make_package.sh --bin-dir $$DESTDIR --arch $$PKG_ARCH --out-dir $$RS_BUILD/dist --strip \"$(STRIP)\" --cxx \"$(CXX)\" ;)
    message("Package: $$RS_BUILD/dist ($$PKG_ARCH)")
}
