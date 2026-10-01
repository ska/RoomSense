#!/bin/sh
# Starts RoomSense and restarts it if it crashes. Stays in the
# foreground, so the launcher's supervision of start.sh still applies.
# Before the first start the Bluetooth module of the panel is reset
# (bluetooth_reset below).
#
# Exit status of the application (see common.h, as AcquaThermoNet):
#   0  stopped on purpose (SIGTERM/SIGINT, clean shutdown with MQTT offline)
#   3  another instance already runs on this device
#   anything else, or killed by a signal: crash -> restart
# Restart delay grows 2, 4, 8, 16, 20s while it keeps crashing within a
# minute, and goes back to 2s after a run of at least a minute. The delay
# stays short: AcquaThermoNet closes a zone after 15 min without readings.

cd "$(dirname "$0")"
SCRIPTDIR=$(pwd)

[ -z "$DISPLAY" ] && export DISPLAY=:0
[ -z "$QT_QPA_PLATFORM" ] && [ -e /tmp/.X0-lock ] && export QT_QPA_PLATFORM=xcb
[ -e "$SCRIPTDIR/plugin/platforminputcontexts/libdbusvirtualkeyboardplugin.so" ] && export QT_IM_MODULE=dbusvirtualkeyboard

export LD_LIBRARY_PATH="$SCRIPTDIR:$SCRIPTDIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$SCRIPTDIR/plugin:$SCRIPTDIR:$QT_PLUGIN_PATH"
export PATH="$SCRIPTDIR:$PATH"

ulimit -c unlimited
# reduce stack size
ulimit -s 1024

log() { echo "RoomSense: $*" | logger -t RoomSense; }

# Bluetooth of the panel: the RSI module on USB (driver rsi_usb/rsi_91x) is
# power cycled through GPIO 92 and its driver reloaded in the BLE mode
# (dev_oper_mode=13), then hci0 is brought up with the hciconfig of this
# package. A failing step is logged and the application starts anyway: it
# keeps retrying the adapter by itself. Skipped on another hardware.
HCI_DEV=hci0
BT_POWER=/sys/class/gpio/gpio92/value

bluetooth_reset() {
    if [ ! -e "$BT_POWER" ] || [ ! -x ./hciconfig ]; then
        log "no GPIO 92 or hciconfig: Bluetooth reset skipped"
        return
    fi
    log "Bluetooth reset ($HCI_DEV)"
    ./hciconfig "$HCI_DEV" down 2>/dev/null
    sleep 0.5
    /bin/echo 0 > "$BT_POWER" || log "cannot switch the Bluetooth module off (GPIO 92)"
    /sbin/rmmod rsi_usb rsi_91x 2>/dev/null
    sleep 2
    /bin/echo 1 > "$BT_POWER" || log "cannot switch the Bluetooth module on (GPIO 92)"
    sleep 0.5
    /sbin/modprobe rsi_usb dev_oper_mode=13 || log "modprobe rsi_usb failed"
    # hci0 appears when the driver has found the module on USB (max 10s)
    i=0
    while [ ! -e "/sys/class/bluetooth/$HCI_DEV" ] && [ $i -lt 50 ]; do
        sleep 0.2
        i=$((i + 1))
    done
    if ./hciconfig "$HCI_DEV" up; then
        log "Bluetooth $HCI_DEV up"
    else
        log "cannot bring $HCI_DEV up"
    fi
}

bluetooth_reset

delay=2
while true; do
    started=$(date +%s)
    ./RoomSense "$@"
    status=$?

    case "$status" in
        0) echo "RoomSense: stopped" | logger -t RoomSense; break ;;
        3) echo "RoomSense: another instance is running, not restarting" | logger -t RoomSense; break ;;
    esac

    if [ $(( $(date +%s) - started )) -ge 60 ]; then
        delay=2
    fi
    echo "RoomSense: exited with status $status, restarting in ${delay}s" | logger -t RoomSense
    sleep $delay
    delay=$(( delay * 2 ))
    [ $delay -gt 20 ] && delay=20
done
