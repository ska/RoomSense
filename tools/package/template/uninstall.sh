#!/bin/sh
# The launcher's update is an uninstall of the old version followed by an
# install of the new one (as found for CanGateway on the same devices), so
# everything not in the package is lost: save the device configuration
# (setting.ini: broker, sensors) and log/. install.sh restores them.

cd "$(dirname "$0")"
BACKUP=/tmp/roomsense-backup

./stop.sh

rm -rf "$BACKUP"
mkdir -p "$BACKUP"
for f in setting.ini; do
    [ -f "deploy/$f" ] && cp -p "deploy/$f" "$BACKUP/"
done
[ -d deploy/log ] && cp -rp deploy/log "$BACKUP/"
sync

echo "RoomSense uninstall: configuration and log saved to $BACKUP" | logger -t RoomSense
exit 0
