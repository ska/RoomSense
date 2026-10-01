#!/bin/sh
# D-Bus name of the panel launcher, looked up on the system bus
launcher=$(dbus-send --system --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ListNames 2>/dev/null | grep -o '"[^"]*\.JMLauncher"' | tr -d '"' | head -n 1)
# Runs once after the package is unzipped into its installation folder.
#  - soname symlinks of the libraries in lib/, if any are bundled (only the
#    real files are in the zip; today none: QtMqtt and libbluetooth are
#    linked statically)
#  - restores setting.ini and log/ saved by uninstall.sh: the
#    launcher's update is "uninstall old + install new", and those files
#    are not part of the package. On a fresh install setting.ini is created
#    by the application itself from setting.default.ini at the first start.
# JMLauncher expects the showProgress/installFinished dbus notifications.

cd "$(dirname "$0")"
BACKUP=/tmp/roomsense-backup

dbus-send --print-reply --system --dest="$launcher" '/' "$launcher".showProgress string:"RoomSense install" int32:-1

chmod +x deploy/RoomSense ./*.sh deploy/*.sh
[ -f deploy/hciconfig ] && chmod +x deploy/hciconfig
result=0

# libX.so.5.13.2 -> libX.so.5.13, libX.so.5, libX.so
for lib in deploy/lib/*.so.*.*.*; do
    [ -f "$lib" ] || continue
    name=$(basename "$lib")
    base=${name%%.so.*}.so
    ver=${name#*.so.}
    major=${ver%%.*}
    minor=${ver#*.}; minor=${minor%%.*}
    ln -sf "$name" "deploy/lib/$base.$major.$minor" || result=1
    ln -sf "$name" "deploy/lib/$base.$major" || result=1
    ln -sf "$name" "deploy/lib/$base" || result=1
done

# Restore the device configuration and state kept across an update, into
# deploy/ (the working directory of the application)
if [ -d "$BACKUP" ]; then
    for f in setting.ini; do
        if [ -f "$BACKUP/$f" ] && [ ! -f "deploy/$f" ]; then
            cp -p "$BACKUP/$f" "deploy/$f" || result=1
        fi
    done
    if [ -d "$BACKUP/log" ] && [ ! -d deploy/log ]; then
        cp -rp "$BACKUP/log" deploy/log || result=1
    fi
    if [ $result -eq 0 ]; then
        rm -rf "$BACKUP"
        echo "RoomSense: setting.ini and log restored after update" | logger -t RoomSense
    else
        echo "RoomSense: restore after update failed, backup kept in $BACKUP" | logger -t RoomSense
    fi
fi
sync

dbus-send --print-reply --system --dest="$launcher" '/' "$launcher".installFinished int32:$result string:""
exit 0
