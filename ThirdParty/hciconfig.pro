# hciconfig of BlueZ (ThirdParty/bluez, 5.50): HCI device configuration
# tool, the same sources as its tools_hciconfig (Makefile.tools), linked
# with the static libbluetooth of libbluetooth.pro. Built by ThirdParty.pro
# into <build>/ThirdParty/bluez/hciconfig; app.pro copies it next to the
# RoomSense binary and the package installs it with it.

TEMPLATE = app
CONFIG  += console warn_off
CONFIG  -= qt app_bundle
TARGET   = hciconfig
DESTDIR  = $$OUT_PWD
# C only: linked by the C compiler (the SDK mkspec links C with g++, which
# brings libstdc++ in when its environment has no -Wl,--as-needed)
QMAKE_LINK = $$QMAKE_CC

BLUEZ = $$PWD/bluez
DEFINES += VERSION=\\\"5.50\\\"

INCLUDEPATH += $$BLUEZ $$OUT_PWD/../include

SOURCES += \
    $$BLUEZ/tools/hciconfig.c \
    $$BLUEZ/tools/csr.c

LIBS           += $$OUT_PWD/../lib/libbluetooth.a
PRE_TARGETDEPS += $$OUT_PWD/../lib/libbluetooth.a
