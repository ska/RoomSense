# RoomSense: tests

What the automatic tests check, suite by suite. How to build and run
them: [`BUILD.md`](BUILD.md) §3; overview in
[`DOCUMENTATION.md`](DOCUMENTATION.md) §17.

| Level | Where | Needs | Size |
|---|---|---|---|
| Unit tests | `tests/`, one binary `tests/tests` | nothing: no BLE, broker, network or display | 16 suites, about 120 test functions |
| End to end | `tools/e2e.sh` | a desktop build of AcquaThermoNet and its repository (simulators), `unshare` | 35 checks, about 2 min |
| Real broker | `tools/broker_check.sh` | the broker credentials | test zones only |

Run the unit tests and the end-to-end test after every change to the
code; add a unit test with every change of `bleparse.cpp`,
`configfile.cpp`, `mqtt.cpp` or `zonecontrol.cpp`.

---

## 1. How the unit tests work

- **QtTest**, one class per suite (`tests/tst_<name>.cpp`), all run in
  order by `tests/main.cpp`, with the `offscreen` platform unless
  `QT_QPA_PLATFORM` is set; the last line says `OK` or `FAIL` and the
  number of failed test functions.
- **No real files**: every test writes its `setting.ini` in a
  `QTemporaryDir`.
- **No BLE**: `BleParse` gets hand-made HCI events; the readings for
  `Mqtt` are built in the test; `FakeScenario` is tested on its own.
- **An in-process MQTT broker** (`tests/fakebroker.h`): records CONNECT
  (client id, keepalive, Will), PUBLISH and SUBSCRIBE, acknowledges QoS 1,
  delivers messages to the subscribers as AcquaThermoNet would, keeps the
  retained ones, can be stopped and started again.
- **No waiting**: ages and timeouts run on `MonoClock`, moved with
  `MonoClock::advanceForTest()`; the collected presses and confirmation
  timeouts of `ZoneControl` are shortened (50 ms, 300 ms).
- **Widgets** (main page, diagnostic page, screen saver, main window) are
  built with a `ZoneControl` fed by hand and checked through their child
  widgets (`objectName`) and simulated mouse clicks.

---

## 2. Unit test suites

### 2.1 `tst_bleparse`: BLE advertisements

| Test | Checks |
|---|---|
| `atc1441`, `atc1441Negative` | atc1441 service data: temperature, humidity, battery, mV, counter; -1.0 °C |
| `pvvx`, `pvvxNegative` | pvvx: 0.01 °C rounded to 0.1, humidity rounded; -12.34 °C |
| `flagsBeforeServiceData` | other AD structures before the service data |
| `twoReports` | two reports in one event, both read (an old offset bug read only the first) |
| `ignored` | unknown 0x181A length, other UUID (BTHome), MAC inside the data not the advertiser's, AD length beyond the data, empty, zero-length AD |
| `truncated` | every prefix of a valid event: no reading, no read past the end |
| `parameterLengthLimits` | parameter length shorter than the buffer; more reports announced than present |
| `otherEvents` | ACL data, command complete, LE connection complete: ignored |
| `normalizeMac` | upper case, spaces trimmed; dashes, short, long, not hex, empty refused |

### 2.2 `tst_config`: `setting.ini`

| Test | Checks |
|---|---|
| `mqttDefaults`, `mqttValues` | `[MQTT]` defaults (localhost, 1883, 120 s) and values |
| `publishIntervalInvalid` | 59, 301, not a number: default kept |
| `longValueTruncated` | a value longer than its buffer is truncated (logged) |
| `sensorMac` | MACs in any case matched; an invalid one never read |
| `sensorZone` | zone from `zone` or the name; duplicate zone, invalid name, reserved `list` and `mode`, too long: not published |
| `ensureSettings` | first start: created from the template, writable; never overwritten; no template: defaults |
| `logInfo` | `[LOG]` path relative to `setting.ini`; empty: no file |
| `screenSaver` | `[SCREENSAVER]`: off, URL schemes (http/https/file; ftp refused), refresh limits |
| `weather` | `[WEATHER]`: provider, coordinates, contact, `poll_s` at least 60, disabled without lon |
| `settingTemplate` | the deployed `setting.default.ini`: valid, five zones, placeholders only (no place name, no coordinates) |

### 2.3 `tst_fakescenario`: generated readings (desktop)

| Test | Checks |
|---|---|
| `normal` | slow drift in 16.5…22.5 °C, deterministic, unknown sensor silent |
| `lostAndBack` | `lost` / `back`: the sensor silent, the others go on |
| `spikeOutStep` | `spike` one reading only, `out` one value, `step` from then on |
| `coldAndNoBattery` | `cold` around -5 °C, `nobatt` battery 0 |
| `presses` | `press` and `mode` events at their time; missing values refused |
| `invalid` | unknown event, missing or extra values, sensor out of range: refused, normal drift only |

### 2.4 `tst_mqtt`: gateway on the broker

With the in-process broker and an AcquaThermoNet "already running"
(retained status and states).

| Test | Checks |
|---|---|
| `connectAndStatus` | client id `RoomSense-<unique_id>`, keepalive, Will `offline` retained, `RoomSense/status online` |
| `payload` | reading topic and JSON: number temperature, humidity, battery, mV, time, MAC; QoS 1, not retained; sent counter on the acknowledgement |
| `negativeTemperature` | negative values |
| `unknownOmitted` | battery, mV, humidity omitted when 0 |
| `noZoneNotPublished` | a sensor without a valid zone: nothing |
| `rate` | the interval, a 0.1 °C change after at least 10 s, nothing in between |
| `zonesIndependent` | each zone its own rate |
| `outOfRange` | below -20 or above 50 °C dropped and counted; the limits included |
| `jumpConfirmed` | a jump over 3 °C waits for a reading at least 10 s later within 0.5 °C |
| `jumpNotConfirmed` | another value: new pending; back near the old value: accepted |
| `firstReadingConfirmed` | the first reading waits for one at least 10 s later within 0.5 °C, not counted as filtered |
| `firstReadingNotConfirmed` | a wrong first reading (45 °C) never published: the next one starts a new confirmation |
| `confirmationAfterSilence` | after 5 min without readings, the next one needs a confirmation again |
| `notBufferedWhileDisconnected` | broker down: readings dropped, reconnect after 1 s, nothing sent late |
| `controllerSubscriptions` | subscriptions to the AcquaThermoNet status, states and mode; the retained state reaches `ZoneControl` |
| `setPointRoundTrip` | presses → one `set_temp` (plain number, QoS 1, not retained) → `state_temp` confirms |
| `houseModeRoundTrip` | `mode/set` → `mode/state` confirms |
| `noCommandWhenControllerOffline` | `status offline`: no command |
| `shutdown` | `offline` published, clean disconnect (no Will), no reconnect |

### 2.5 `tst_zonecontrol`: setpoints and house mode

| Test | Checks |
|---|---|
| `normalize` | rounded to 0.5, clamped to 5…25 |
| `notReachableAtStart` | no command before the controller state arrives |
| `pressesCollected` | the value shown at once as pending, one command after the presses |
| `confirmationMayDiffer` | a confirmation with another value (clamped, or changed elsewhere) ends the pending |
| `stateBeforeTheCommandKeepsPending` | a `state_temp` that arrives before the command is sent does not confirm it |
| `clamped` | presses beyond the limits |
| `notConfirmed` | no confirmation: back to the confirmed value after the timeout |
| `controllerOffline`, `brokerDisconnected` | the change dropped, the last value kept |
| `ignored` | other zones, a payload that is not a number, a sensor without zone |
| `stepAll`, `stepAllNotPossible` | all rooms: align to the lowest / highest, then ±0.5, one command per zone, clamped; not during a house mode |
| `houseMode` | request pending, a setpoint change dropped by it, −/+ disabled during a mode |
| `windowCountdown` | `remaining_s` counted down, never negative, replaced by a new `mode/state` |
| `modeNotConfirmed`, `modeOffline`, `modeStateInvalid` | timeout, offline, invalid JSON |
| `heat` | `state_mode` heat / off |

### 2.6 `tst_chrono`: chrono program and state

| Test | Checks |
|---|---|
| `parseProgram`, `parseProgramInvalid` | `<zone>/chrono/profile`: profiles, holiday days, off and empty; invalid payloads leave the program untouched |
| `parseState` | `<zone>/chrono`: on, manual, paused, next change; no next change without the controller clock; off; invalid |
| `programCommand` | `chrono/profile/set` JSON (no `holiday_days`), compact; the controller's answer is the same program |
| `sameProgram` | comparison after sorting and rounding; `enabled` and profiles count, `edited` and holiday days do not |
| `current`, `next` | slot in force and next change across midnight and weekday/holiday |
| `offAndEmpty` | chrono off: no slot but still drawn; empty holiday profile = weekday |
| `dayTemps` | the 96 quarters of an hour of a day, hourly steps |
| `editSteps` | time to the 15-minute grid, never past the neighbours or midnight; setpoint ±0.5 within 5…25 |
| `splitSlot` | split in the middle on the grid, last slot until midnight, too short, at most 8, empty profile |

### 2.7 `tst_zoneview`: zone card and house button texts

| Test | Checks |
|---|---|
| `normal` | all the texts of a card with a fresh reading and a confirmed setpoint |
| `levels` | `updated … ago` up to 5 min, `last reading … ago` up to 15 min, then `NO SENSOR for …` |
| `lost`, `neverHeard` | no old value shown, zone off for the controller, setpoint still changeable |
| `unknownValues`, `negative` | `--` for unknown humidity and battery, low battery below 20 %; negative temperatures |
| `heat` | heating, idle, unknown, off with no sensor |
| `setPoints` | confirmed, `sending…`, `last known`, `no setpoint`, `no zone` |
| `houseMode` | the mode value with `windows open`, `away`, `boost` |
| `modeButtonTexts` | house button: name, icon, time left, `Sending…`, unknown |
| `age` | `30 s`, `7 min`, `3 h` |

### 2.8 `tst_mainpage`: main page and house popup

| Test | Checks |
|---|---|
| `reading` | a reading on the card: temperature, battery low property |
| `plusSendsOneCommand` | + shows `sending…`, one command, `setpoint` after the confirmation |
| `noSetPointYet`, `controllerOffline` | −/+ disabled |
| `houseModePopup` | the three modes, away disables −/+ and windows open, boost ends away, the active one goes back to normal, a press outside closes |
| `allRooms` | all rooms −/+, popup stays open, disabled during a mode |
| `popupClosesWhenIdle` | closed after the idle time, a press restarts it |
| `longPressOnClock` | short press nothing, held: diagnostics requested |

### 2.9 `tst_diagview`, `tst_diagpage`: diagnostics

| Test | Checks |
|---|---|
| `formats` | uptime, sizes and times as shown |
| `systemHealthy`, `systemWeather`, `systemProblems` | the nine System rows and their levels: broker, controller, BLE adapter, advertisements, watchdog, log, weather |
| `sensorRows` | per sensor: RSSI, last reading, sent, dropped; never heard, lost |
| `system`, `sensors`, `tabsAndBack` (`tst_diagpage`) | the page built from the real objects, sensor cards, the tabs and Back |

### 2.10 `tst_screensaver`, `tst_mainwindow`: screen saver and pages

| Test | Checks |
|---|---|
| `summaryAllOk`, `summaryAlarms` | summary lines: all ok, sensors lost, battery low (0 = unknown, no alarm), house mode with time left; rooms heating (a lost zone is not) |
| `webcamImage` | image from a file URL (same path as http); a missing file keeps the last image |
| `idleAndWake` | screen saver after the idle time, a press wakes the main page, presses keep it, house popup closed under it, idle 0 = off |
| `pages` (`tst_mainwindow`) | starts on the main page; long press on the clock: diagnostics; Back: main page |

### 2.11 `tst_weather`: outdoor weather

| Test | Checks |
|---|---|
| `parseWttr` | wttr.in one-line answer, no icon |
| `parseMetNo`, `parseMetNoInvalid` | met.no JSON (also the last steps with `next_6_hours` only); empty, error page, no series, no temperature |
| `httpDate` | `Expires` / `Last-Modified` |
| `icons` | met.no symbol codes → `wx-*` icons (day, night, fair, cloudy, fog, rain, sleet, snow, thunder) |
| `view` | screen saver weather line; hidden without data or after 3 h |

### 2.12 System suites

| Suite | Test | Checks |
|---|---|---|
| `tst_singleinstance` | `secondInstanceRefused`, `freedOnDestruction`, `freedWhenKilled` | one owner per name; freed on exit and after SIGKILL (the restart loop can start again) |
| `tst_watchdog` | `heartbeat`, `missingDevice`, `healthCheck` | stalled thread detected; no device: harmless; refresh stops while the health check fails (said once), magic close on a clean exit |
| `tst_logging` | `rotation`, `recentMessages` | rotation and old files; the last messages for the Log tab, newest first |

---

## 3. End to end: `tools/e2e.sh`

```sh
tools/e2e.sh <RoomSense> <AcquaThermoNet> <HA_Hmi_Relay dir>
```

Both real applications (desktop builds, offscreen), the AcquaThermoNet
simulators (`tools/minibroker.py` on port 18830, `tools/mbsim.py`), in a
network namespace of their own (`unshare -rn`): the single instance locks
are per namespace, so instances the user runs are not disturbed, and no
real broker is reachable. AcquaThermoNet: zones `salotto` and `camera` at
20 °C, sensor timeout 15 s, frost protection and valve exercise off.
RoomSense: the two sensors, generated readings with `RS_FAKE` events.

| Phase | Stimulus | Checks |
|---|---|---|
| 1. RoomSense publishes | start; a spike (+8 °C) at 20 s and an 85 °C reading at 24 s on salotto | client id, `RoomSense/status online` retained, AcquaThermoNet sees RoomSense online; the first reading waits for confirmation; readings as JSON numbers with the MAC, not retained, no `Invalid sensor data` at AcquaThermoNet; the spike waits for confirmation and the 85 °C is dropped, neither published |
| 2. RoomSense stopped | SIGTERM | exit code 0, `RoomSense/status offline`, AcquaThermoNet sees RoomSense offline; both sensors lost, zones off |
| 3. Back, panel commands | restart; camera lost while running; +2 on salotto; windows open, normal, boost, normal from the house popup | sensors back; one `set_temp 21`, not retained, confirmed by `state_temp 21`; `mode/set window`, not retained, `mode/state` with countdown, salotto at 8, RoomSense shows the mode; `normal`: salotto back to 21; boost: salotto at 25, countdown; camera lost while RoomSense runs, salotto still heard; AcquaThermoNet saved 21 |
| 4. End | SIGTERM to both | AcquaThermoNet exit code 0 |

On failure the script prints the tail of the broker log and both
application logs.

---

## 4. Real broker: `tools/broker_check.sh`

```sh
MQTT_PASSWORD=... tools/broker_check.sh <RoomSense> <host> <port> <user>
```

A smoke test against the real broker: RoomSense with generated readings
on the test zones `e2e_test_1` and `e2e_test_2`, which no AcquaThermoNet
knows (the heating is not affected); the read-only subscriber
`tools/mqttsub.py` checks what the broker delivers. The only visible
effect is `RoomSense/status` going online, then offline. The password
comes from the environment, never from the command line.

---

## 5. Not covered automatically

To check by hand, on the desktop or on the house panel:

- the real BLE scan (HCI socket, adapter retries, the Bluetooth reset of
  `start.sh`) and the real sensors;
- the look of the pages (sizes, touch, scrolling): a scratch program that
  builds a page with fake data and saves `widget->grab()`;
- network services: met.no / wttr.in, the webcam URL;
- the panel: libQt5Svg, JPEG plugin, watchdog, launcher install and
  update, boot splash.

---

## 6. Adding a test

- A new case: a private slot in `tests/tst_<name>.cpp` (`_data()` for a
  table of cases).
- A new suite: `tests/tst_<name>.cpp` with `run<Name>Tests()`, added to
  `tests/tests.pro` (with the sources it needs) and called in
  `tests/main.cpp` (before `runLoggingTests`, which must stay last).
- A new check with AcquaThermoNet: an `RS_FAKE` event (`fakescenario.*`)
  and a `check` in `tools/e2e.sh`.
- Then update this document.
