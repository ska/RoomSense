# Libraries used by the application and missing in the Qt in use, built
# from the git submodules by ThirdParty.pro as static libraries, as
# AcquaThermoNet (HA_Hmi_Relay ThirdParty/qtmodules.pri):
#
#   QtMqtt        not in any of the Qt used (desktop 5.13.2, device SDK):
#                 always from ThirdParty/qtmqtt (v5.13.2), static
#   libbluetooth  missing in the device SDK: from ThirdParty/bluez (5.50),
#                 static, library only (ThirdParty/libbluetooth.pro); ARM
#                 only, the desktop build uses generated test readings
#   hciconfig     BlueZ tool, with that library (ThirdParty/hciconfig.pro),
#                 copied next to the RoomSense binary (RS_HCICONFIG)
#
# Include it in a project to link them; ThirdParty.pro must be built first
# (the top level RoomSense.pro takes care of the order).

RS_THIRDPARTY_BUILD = $$RS_BUILD/ThirdParty

# CPU of the house panel: Cortex-A9, while the SDK compiler line has
# -mcpu=cortex-a7. The A9 has no hardware integer divide: code built for
# the A7 (sdiv/udiv) dies with "Illegal instruction" there (QtMqtt at the
# first publish). gcc takes the last -mcpu: added to our code and to the
# static ThirdParty libraries. Another panel: qmake RS_ARM_CPU=<cpu>.
RS_CPU_FLAGS =
equals(QT_ARCH, arm) {
    isEmpty(RS_ARM_CPU): RS_ARM_CPU = cortex-a9
    RS_CPU_FLAGS = -mcpu=$$RS_ARM_CPU
}

# Real BLE reader on the device only
RS_BLE = false
equals(QT_ARCH, arm)|equals(QT_ARCH, arm64): RS_BLE = true

# Consumer side (application): skipped by ThirdParty.pro itself
!equals(TEMPLATE, aux) {
    QT += network
    QMAKE_CFLAGS   += $$RS_CPU_FLAGS
    QMAKE_CXXFLAGS += $$RS_CPU_FLAGS

    INCLUDEPATH    += $$RS_THIRDPARTY_BUILD/qtmqtt/include
    LIBS           += $$RS_THIRDPARTY_BUILD/qtmqtt/lib/libQt5Mqtt.a
    PRE_TARGETDEPS += $$RS_THIRDPARTY_BUILD/qtmqtt/lib/libQt5Mqtt.a

    equals(RS_BLE, true) {
        RS_HCICONFIG    = $$RS_THIRDPARTY_BUILD/bluez/hciconfig/hciconfig
        INCLUDEPATH    += $$RS_THIRDPARTY_BUILD/bluez/include
        LIBS           += $$RS_THIRDPARTY_BUILD/bluez/lib/libbluetooth.a
        PRE_TARGETDEPS += $$RS_THIRDPARTY_BUILD/bluez/lib/libbluetooth.a
    }
}
