#!/bin/sh
# D-Bus name of the panel launcher, looked up on the system bus
launcher=$(dbus-send --system --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus.ListNames 2>/dev/null | grep -o '"[^"]*\.JMLauncher"' | tr -d '"' | head -n 1)
# The launcher does updates as uninstall + install (uninstall.sh saves and
# install.sh restores the configuration and log): nothing to do.
echo "RoomSense update: nothing to do" | logger -t RoomSense
dbus-send --print-reply --system --dest="$launcher" '/' "$launcher".updateFinished int32:0 string:""
exit 0
