# Builds the libraries from the git submodules as static libraries, with
# the qmake of the Qt in use (desktop or cross SDK), into
# <build>/ThirdParty/<module>. Only when the library is missing or the
# options changed (options.stamp): after changing a submodule, delete
# <build>/ThirdParty (or run a clean).
# See thirdparty.pri for which library comes from where.
#
# First checkout: git submodule update --init

TEMPLATE = aux
include(thirdparty.pri)

# Yocto SDK mkspec (linux-oe-g++): QMAKE_AR = $$(OE_QMAKE_AR) cqs. A Qt
# Creator kit may set the compilers but not OE_QMAKE_AR (the SDK
# environment-setup script does): the archiver is then just "cqs" and make
# fails with error 127. Give the module builds an archiver: $AR, else the
# gcc-ar of the compiler, as the mkspec fallback; also to make, which runs
# qmake again in the QtMqtt subdirectories.
RS_QMAKE_ENV =
RS_CPU_ARGS =
!isEmpty(RS_CPU_FLAGS): RS_CPU_ARGS = \"QMAKE_CFLAGS+=$$RS_CPU_FLAGS\" \"QMAKE_CXXFLAGS+=$$RS_CPU_FLAGS\"
equals(QMAKE_AR, cqs) {
    RS_AR = $$(AR)
    isEmpty(RS_AR) {
        RS_AR = $$split(QMAKE_CC, " ")      # one string with the flags
        RS_AR = $$first(RS_AR)
        RS_AR ~= s/gcc$/gcc-ar/
    }
    RS_QMAKE_ENV = OE_QMAKE_AR=$$RS_AR
    message("OE_QMAKE_AR not set, ThirdParty archiver: $$RS_AR")
}

# The options the libraries are compiled with: written to a stamp file only
# when they change, and every library depends on it, so a change (e.g.
# RS_ARM_CPU) rebuilds them from scratch; a build without the stamp too.
RS_OPTIONS = cpu=$$RS_ARM_CPU
RS_STAMP = $$RS_THIRDPARTY_BUILD/options.stamp
!equals(RS_OPTIONS, $$cat($$RS_STAMP, lines)) {
    !write_file($$RS_STAMP, RS_OPTIONS): error("cannot write $$RS_STAMP")
    message("ThirdParty options changed ($$RS_OPTIONS): libraries rebuilt")
}

# $$1 = submodule dir (qtmqtt), $$2 = library (Qt5Mqtt)
defineTest(rsBuildQtModule) {
    src = $$PWD/$$1
    out = $$RS_THIRDPARTY_BUILD/$$1
    lib = $$out/lib/lib$${2}.a
    !exists($$src/$${1}.pro): \
        error("ThirdParty/$$1 is empty: run 'git submodule update --init'")

    name = build_$$1
    $${name}.target   = $$lib
    $${name}.depends  = $$RS_STAMP
    $${name}.commands = rm -rf $$out && mkdir -p $$out && cd $$out && \
        $$RS_QMAKE_ENV $$QMAKE_QMAKE $$src/$${1}.pro CONFIG+=static CONFIG+=release CONFIG-=debug_and_release $$RS_CPU_ARGS && \
        $$RS_QMAKE_ENV $(MAKE) sub-src
    export($${name}.target)
    export($${name}.depends)
    export($${name}.commands)
    QMAKE_EXTRA_TARGETS += $$name
    PRE_TARGETDEPS += $$lib
    export(QMAKE_EXTRA_TARGETS)
    export(PRE_TARGETDEPS)
    return(true)
}

rsBuildQtModule(qtmqtt, Qt5Mqtt)

# libbluetooth and the hciconfig tool only (no daemon, no other tools):
# BlueZ builds with autotools, those sources are compiled here by
# libbluetooth.pro and hciconfig.pro instead
equals(RS_BLE, true) {
    !exists($$PWD/bluez/lib/bluetooth.c): \
        error("ThirdParty/bluez is empty: run 'git submodule update --init'")
    build_bluez.target   = $$RS_THIRDPARTY_BUILD/bluez/lib/libbluetooth.a
    build_bluez.depends  = $$RS_STAMP
    build_bluez.commands = rm -rf $$RS_THIRDPARTY_BUILD/bluez && mkdir -p $$RS_THIRDPARTY_BUILD/bluez && cd $$RS_THIRDPARTY_BUILD/bluez && \
        $$RS_QMAKE_ENV $$QMAKE_QMAKE $$PWD/libbluetooth.pro CONFIG+=release CONFIG-=debug_and_release $$RS_CPU_ARGS && \
        $$RS_QMAKE_ENV $(MAKE)
    QMAKE_EXTRA_TARGETS += build_bluez
    PRE_TARGETDEPS += $$build_bluez.target

    # hciconfig, with that library (hciconfig.pro)
    build_hciconfig.target   = $$RS_THIRDPARTY_BUILD/bluez/hciconfig/hciconfig
    build_hciconfig.depends  = $$build_bluez.target
    build_hciconfig.commands = mkdir -p $$RS_THIRDPARTY_BUILD/bluez/hciconfig && cd $$RS_THIRDPARTY_BUILD/bluez/hciconfig && \
        $$RS_QMAKE_ENV $$QMAKE_QMAKE $$PWD/hciconfig.pro CONFIG+=release CONFIG-=debug_and_release $$RS_CPU_ARGS && \
        $$RS_QMAKE_ENV $(MAKE)
    QMAKE_EXTRA_TARGETS += build_hciconfig
    PRE_TARGETDEPS += $$build_hciconfig.target
}
