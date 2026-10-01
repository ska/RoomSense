#!/bin/sh
# Stops RoomSense with SIGTERM: the application stops the BLE scan,
# publishes MQTT "offline", disconnects (max 5s each) and exits 0, which
# also ends the start.sh restart loop.
# Waits for the exit so that an update never replaces a running binary;
# after 15s the process is killed.

pkill -TERM -x RoomSense 2>/dev/null || exit 0

i=0
while pidof RoomSense >/dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 15 ]; then
        echo "RoomSense: not stopped after 15s, killing it" | logger -t RoomSense
        pkill -KILL -x RoomSense 2>/dev/null
        break
    fi
    sleep 1
done
exit 0
