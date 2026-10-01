# RoomSense: open this file in Qt Creator or run qmake on it.
#
#   ThirdParty   libraries missing in the Qt in use, static, from the git
#                submodules (first checkout: git submodule update --init):
#                QtMqtt always, libbluetooth (BlueZ) for the ARM device
#   app          the application -> <build>/bin_<arch>/RoomSense
#
# Optional, qmake arguments:
#   CONFIG+=tests   unit tests  -> <build>/tests/tests

TEMPLATE = subdirs

SUBDIRS += ThirdParty app
app.depends = ThirdParty

CONFIG(tests) {
    SUBDIRS += tests
    tests.depends = ThirdParty
}

OTHER_FILES += \
    .qmake.conf \
    tools/package/README.md \
    tools/package/make_package.sh \
    setting.default.ini \
    version.pri \
    version.h.in \
    ThirdParty/thirdparty.pri \
    ThirdParty/libbluetooth.pro
