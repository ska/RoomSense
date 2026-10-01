# libbluetooth of BlueZ (ThirdParty/bluez, 5.50) as a static library, the
# same sources as its lib_libbluetooth_la (Makefile.am). Built by
# ThirdParty.pro into <build>/ThirdParty/bluez:
#   lib/libbluetooth.a
#   include/bluetooth/*.h     (as the installed BlueZ headers)

TEMPLATE = lib
CONFIG  += staticlib warn_off
CONFIG  -= qt
TARGET   = bluetooth
DESTDIR  = $$OUT_PWD/lib

BLUEZ = $$PWD/bluez/lib

SOURCES += \
    $$BLUEZ/bluetooth.c \
    $$BLUEZ/hci.c \
    $$BLUEZ/sdp.c

BLUEZ_HEADERS = bluetooth.h hci.h hci_lib.h sco.h l2cap.h sdp.h sdp_lib.h \
                rfcomm.h bnep.h cmtp.h hidp.h

# The sources include <bluetooth/...>: the headers are copied there when
# qmake runs, so they exist before any compilation (a make target raced
# with the parallel compilation of sdp.c)
INCLUDEPATH += $$OUT_PWD/include
HEADERS_OUT = $$OUT_PWD/include/bluetooth
!system(mkdir -p $$shell_quote($$HEADERS_OUT)): error("cannot create $$HEADERS_OUT")
for(h, BLUEZ_HEADERS) {
    !system(cp -f $$shell_quote($$BLUEZ/$$h) $$shell_quote($$HEADERS_OUT/$$h)): \
        error("cannot copy $$BLUEZ/$$h")
}
