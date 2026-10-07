#!/bin/bash
#
# Smoke test against a real broker: RoomSense (generated readings) publishes
# on TEST zones that no AcquaThermoNet knows (e2e_test_1, e2e_test_2), a
# read-only subscriber (tools/mqttsub.py) checks what the broker delivers.
# The controller ignores unknown zones: the heating is not affected. The
# only visible effect is RoomSense/status going online, then offline.
#
#   MQTT_PASSWORD=... tools/broker_check.sh PATH/TO/RoomSense HOST PORT USER
#
# The password comes from the environment, never from the command line.
# No other RoomSense may run on this PC (single instance).
# Exit code 0 when every check passes.
#
set -u

RS=$(realpath "${1:?usage: MQTT_PASSWORD=... $0 RoomSense HOST PORT USER}")
HOST=${2:?host}
PORT=${3:?port}
USER_NAME=${4:?user}
TOOLS=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
FAILS=0
PIDS=()

cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

T='^[0-9.]+ '
check() {   # check DESCRIPTION FILE PATTERN
    if grep -qE "$3" "$2"; then echo "PASS  $1"; else echo "FAIL  $1"; FAILS=$((FAILS+1)); fi
}
check_not() {
    if grep -qE "$3" "$2"; then echo "FAIL  $1"; FAILS=$((FAILS+1)); else echo "PASS  $1"; fi
}

export QT_QPA_PLATFORM=offscreen
export DBUS_SESSION_BUS_ADDRESS=disabled:

cd "$WORK"
cat > setting.ini <<EOF2
[MQTT]
broker_addr=$HOST
broker_port=$PORT
broker_uname=$USER_NAME
broker_password=${MQTT_PASSWORD:-}
unique_id=e2e

[LOG]
file=

[SENSORS]
e2e_test_1\\mac=A4:C1:38:00:00:01
e2e_test_1\\name=e2e_test_1
e2e_test_2\\mac=A4:C1:38:00:00:02
e2e_test_2\\name=e2e_test_2
EOF2
chmod 600 setting.ini

python3 "$TOOLS/mqttsub.py" "$HOST" "$PORT" "$USER_NAME" 25 'RoomSense/status' 'RoomSense/e2e_test_1/data' 'RoomSense/e2e_test_2/data' > sub.log 2> sub.err & SUB=$!
PIDS+=($SUB)
sleep 2
"$RS" > app.log 2>&1 & RS_PID=$!
sleep 15
kill -TERM $RS_PID
wait $RS_PID
RC=$?
wait $SUB

if [ -s sub.err ]; then echo "FAIL  subscriber: $(cat sub.err)"; FAILS=$((FAILS+1)); else echo "PASS  subscriber connected"; fi
check "RoomSense connected"             app.log  'MQTT client connected to broker'
check "RoomSense/status online"         sub.log  "${T}RoomSense/status online"
check "e2e_test_1 reading delivered"    sub.log  "${T}RoomSense/e2e_test_1/data \{.*\"temperature\":"
check "e2e_test_2 reading delivered"    sub.log  "${T}RoomSense/e2e_test_2/data \{.*\"temperature\":"
check_not "readings not retained"       sub.log  "${T}RoomSense/[^ /]+/data .* \[R\]$"
check "RoomSense/status offline"        sub.log  "${T}RoomSense/status offline"
if [ $RC -eq 0 ]; then echo "PASS  RoomSense exit code 0"; else echo "FAIL  RoomSense exit code $RC"; FAILS=$((FAILS+1)); fi
if [ -n "${MQTT_PASSWORD:-}" ] && grep -qF "$MQTT_PASSWORD" app.log; then echo "FAIL  password in the log"; FAILS=$((FAILS+1)); else echo "PASS  password not in the log"; fi

if [ $FAILS -ne 0 ]; then
    echo "$FAILS check(s) failed"
    echo "--- received"; cat sub.log
    echo "--- RoomSense"; cat app.log
    exit 1
fi
echo "all checks passed"
