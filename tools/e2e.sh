#!/bin/bash
#
# End to end test on the development PC: RoomSense (generated readings)
# and AcquaThermoNet, the real applications, through the minimal MQTT
# broker and the relay board simulator of AcquaThermoNet
# (tools/minibroker.py, tools/mbsim.py of its repository).
#
#   tools/e2e.sh PATH/TO/RoomSense PATH/TO/AcquaThermoNet PATH/TO/HA_Hmi_Relay
#
# (e.g. <build>/bin_x86_64/RoomSense). Runs in its own network namespace
# (unshare -rn, no root needed): the single instance locks of both
# applications are per namespace, so an instance the user is running is
# not disturbed, and the test cannot reach any real broker.
# Generated readings with the events of RS_FAKE (fakescenario.h): a spike
# and an out of range value (filtered by RoomSense), a sensor lost while
# RoomSense runs (detected by AcquaThermoNet), a setpoint changed on the
# RoomSense panel (confirmed by AcquaThermoNet), windows open, boost and
# back to normal from the house mode popup (setpoints changed, restored).
# Exit code 0 when every check passes; about two minutes (AcquaThermoNet
# checks the sensor timeouts every minute).
#
set -u

TIMEOUT_S=15                # AcquaThermoNet sensor_timeout_s for the test
CHECK_S=60                  # AcquaThermoNet checks the timeouts every 60 s

if [ -z "${RS_E2E_NETNS:-}" ]; then
    RS_E2E_NETNS=1 exec unshare -rn "$0" "$@"
fi
ip link set lo up

RS=$(realpath "${1:?usage: $0 RoomSense AcquaThermoNet HA_Hmi_Relay_dir}")
ATN=$(realpath "${2:?usage: $0 RoomSense AcquaThermoNet HA_Hmi_Relay_dir}")
ATN_TOOLS=$(realpath "${3:?usage: $0 RoomSense AcquaThermoNet HA_Hmi_Relay_dir}")/tools
PORT=18830
WORK=$(mktemp -d)
FAILS=0
PIDS=()

cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

check() {   # check DESCRIPTION FILE PATTERN
    if grep -qE "$3" "$2"; then echo "PASS  $1"; else echo "FAIL  $1"; FAILS=$((FAILS+1)); fi
}
wait_for() {    # wait_for FILE PATTERN SECONDS: until PATTERN is in FILE
    for _ in $(seq $(($3 * 5))); do grep -qE "$2" "$1" 2>/dev/null && return 0; sleep 0.2; done
    return 1
}
# broker.log lines are "<epoch> <topic> <payload>[ [R]]": anchor the topics
# (the Home Assistant discovery payload of AcquaThermoNet names the
# RoomSense topics too)
T='^[0-9.]+ '
check_not() {   # check_not DESCRIPTION FILE PATTERN
    if grep -qE "$3" "$2"; then echo "FAIL  $1"; FAILS=$((FAILS+1)); else echo "PASS  $1"; fi
}

export QT_QPA_PLATFORM=offscreen
export DBUS_SESSION_BUS_ADDRESS=disabled:   # no show/hide service in the test

cd "$WORK"
mkdir atn rs
python3 "$ATN_TOOLS/minibroker.py" $PORT broker.log & PIDS+=($!)
python3 "$ATN_TOOLS/mbsim.py" sim.log > pty.txt & PIDS+=($!)
for _ in $(seq 25); do [ -s pty.txt ] && break; sleep 0.2; done

cat > atn/setting.ini <<EOF
[MQTT]
broker_addr=127.0.0.1
broker_port=$PORT
unique_id=E2E

[ZONES]
list=salotto, camera
salotto\\setpoint=20
camera\\setpoint=20

[RELAY]
salotto\\relaynum=5
camera\\relaynum=4

[SERIAL]
port=$(cat pty.txt)

[REGULATION]
min_cycle_s=0
sensor_timeout_s=$TIMEOUT_S

[FROST_PROTECTION]
enabled=false

[VALVE_EXERCISE]
enabled=false

[LOG]
file=log/app.log

[TELEGRAM]
enabled=false
EOF

cat > rs/setting.ini <<EOF
[MQTT]
broker_addr=127.0.0.1
broker_port=$PORT
unique_id=e2e
publish_interval_s=60

[LOG]
file=log/RoomSense.log

[SENSORS]
salotto\\mac=A4:C1:38:00:00:01
salotto\\name=salotto
camera\\mac=A4:C1:38:00:00:02
camera\\name=camera
EOF

(cd atn && exec "$ATN" > app.log 2>&1) & ATN_PID=$!
sleep 3

echo "--- RoomSense publishes, AcquaThermoNet accepts"
# RS_FAKE sensor index: [SENSORS] groups in alphabetical order, camera=0 salotto=1
# salotto: a spike at 20 s and an out of range reading at 24 s, after the
# first reading confirmed (about 10 s)
(cd rs && RS_FAKE="spike=1@20:+80;out=1@24:850" exec "$RS" > app.log 2>&1) & RS_PID=$!
wait_for broker.log "${T}RoomSense/salotto/data " 20
wait_for broker.log "${T}RoomSense/camera/data " 20
wait_for rs/log/RoomSense.log 'out of range, dropped' 30
check "client id RoomSense-e2e"         rs/log/RoomSense.log 'MQTT client id: +"RoomSense-e2e"'
check "RoomSense/status online"         broker.log "${T}RoomSense/status online \[R\]"
check "salotto reading, JSON number"    broker.log "${T}RoomSense/salotto/data \{.*\"temperature\":-?[0-9]+(\.[0-9])?[,}]"
check "camera reading"                  broker.log "${T}RoomSense/camera/data \{.*\"mac\":\"A4:C1:38:00:00:02\""
check_not "readings not retained"       broker.log "${T}RoomSense/[^ /]+/data .* \[R\]$"
check_not "no invalid sensor data"      atn/app.log 'Invalid sensor data'
check "first reading confirmed first"   rs/log/RoomSense.log 'Sensor salotto: first reading .* waiting for confirmation'
check "AcquaThermoNet: RoomSense online" atn/app.log 'RoomSense gateway online'
check "spike waits for confirmation"    rs/log/RoomSense.log 'Sensor salotto: jump .* waiting for confirmation'
check "85.0 dropped"                    rs/log/RoomSense.log 'Sensor salotto: temperature 85 out of range, dropped'
check_not "spike and 85.0 not published" broker.log "${T}RoomSense/salotto/data .*\"temperature\":(2[5-9]|[3-9][0-9])"

echo "--- RoomSense stopped: offline, then sensor lost"
kill -TERM $RS_PID
wait $RS_PID
RS_RC=$?
if [ $RS_RC -eq 0 ]; then echo "PASS  RoomSense exit code 0"; else echo "FAIL  RoomSense exit code $RS_RC"; FAILS=$((FAILS+1)); fi
check "RoomSense/status offline"        broker.log "${T}RoomSense/status offline \[R\]"
wait_for atn/app.log 'RoomSense gateway offline' 5
check "AcquaThermoNet: RoomSense offline" atn/app.log 'RoomSense gateway offline'
wait_for atn/app.log 'Sensor timeout zone "camera"' $((TIMEOUT_S + CHECK_S + 10))
wait_for atn/app.log 'Sensor timeout zone "salotto"' 5
check "salotto: lost after data, OFF"   atn/app.log 'Sensor timeout zone "salotto" last data .* zone OFF'
check "camera: lost after data, OFF"    atn/app.log 'Sensor timeout zone "camera" last data .* zone OFF'

echo "--- RoomSense back, camera lost while running, setpoint of salotto changed on the panel,"
echo "    then windows open, boost and back to normal from the house mode popup"
(cd rs && RS_FAKE="lost=0@16;press=1@8:+2;mode=window@14;mode=normal@20;mode=boost@24;mode=normal@28" exec "$RS" > app2.log 2>&1) & RS_PID=$!
wait_for atn/app.log 'Sensor back zone "salotto"' 25
wait_for atn/app.log 'Sensor back zone "camera"' 25
check "salotto: sensor back"            atn/app.log 'Sensor back zone "salotto"'
check "camera: sensor back"             atn/app.log 'Sensor back zone "camera"'
wait_for broker.log "${T}AcquaThermoNet/salotto/state_temp 21 " 15
check "panel +2: one set_temp 21"       broker.log "${T}AcquaThermoNet/salotto/set_temp 21$"
check_not "set_temp not retained"       broker.log "${T}AcquaThermoNet/salotto/set_temp .*\[R\]$"
check "AcquaThermoNet confirms 21"      broker.log "${T}AcquaThermoNet/salotto/state_temp 21 \[R\]"
wait_for broker.log "${T}AcquaThermoNet/mode/set normal$" 25
wait_for atn/app.log 'House mode window -> normal' 5
check "panel: mode/set window"          broker.log "${T}AcquaThermoNet/mode/set window$"
check_not "mode/set not retained"       broker.log "${T}AcquaThermoNet/mode/set .*\[R\]$"
check "AcquaThermoNet: window, countdown" broker.log "${T}AcquaThermoNet/mode/state \{\"mode\":\"window\",\"remaining_s\":[0-9]+\} \[R\]"
check "windows open: salotto at 8"      broker.log "${T}AcquaThermoNet/salotto/state_temp 8 \[R\]"
check "RoomSense shows windows open"    rs/log/RoomSense.log 'House mode window'
check "panel: mode/set normal"          broker.log "${T}AcquaThermoNet/mode/set normal$"
# after normal: salotto back to its own 21
if awk '/AcquaThermoNet\/mode\/set normal$/ { n = 1 } n && /AcquaThermoNet\/salotto\/state_temp 21 \[R\]/ { ok = 1 } END { exit !ok }' broker.log
then echo "PASS  normal: salotto back to 21"; else echo "FAIL  normal: salotto back to 21"; FAILS=$((FAILS+1)); fi
wait_for atn/app.log 'House mode boost -> normal' 15
check "panel: mode/set boost"           broker.log "${T}AcquaThermoNet/mode/set boost$"
check "boost: salotto at 25"            broker.log "${T}AcquaThermoNet/salotto/state_temp 25 \[R\]"
check "AcquaThermoNet: boost, countdown" broker.log "${T}AcquaThermoNet/mode/state \{\"mode\":\"boost\",\"remaining_s\":[0-9]+\} \[R\]"
for _ in $(seq $(( (TIMEOUT_S + CHECK_S + 10) * 5 ))); do
    [ "$(grep -c 'Sensor timeout zone "camera"' atn/app.log)" -ge 2 ] && break; sleep 0.2
done
if [ "$(grep -c 'Sensor timeout zone "camera"' atn/app.log)" -ge 2 ]; then echo "PASS  camera lost while RoomSense runs"; else echo "FAIL  camera lost while RoomSense runs"; FAILS=$((FAILS+1)); fi
if [ "$(grep -c 'Sensor timeout zone "salotto"' atn/app.log)" -eq 1 ]; then echo "PASS  salotto still heard"; else echo "FAIL  salotto still heard"; FAILS=$((FAILS+1)); fi
check "AcquaThermoNet saved salotto 21" atn/app.log 'Save setpoint zone "?salotto"? = 21'   # flash write delayed
kill -TERM $RS_PID
wait $RS_PID

kill -TERM $ATN_PID
wait $ATN_PID
ATN_RC=$?
if [ $ATN_RC -eq 0 ]; then echo "PASS  AcquaThermoNet exit code 0"; else echo "FAIL  AcquaThermoNet exit code $ATN_RC"; FAILS=$((FAILS+1)); fi

if [ $FAILS -ne 0 ]; then
    echo "$FAILS check(s) failed"
    echo "--- broker";          grep -v '^$' broker.log | tail -40
    echo "--- RoomSense";       cat rs/app.log
    echo "--- AcquaThermoNet";  tail -40 atn/app.log
    exit 1
fi
echo "all checks passed"
