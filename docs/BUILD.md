# RoomSense: build, test, package

A quick reference for building RoomSense for each target, running the
tests and making the package zip for the house panel. More in
[`DOCUMENTATION.md`](DOCUMENTATION.md) §15–17 and
[`tools/package/README.md`](../tools/package/README.md).

Always build **outside the source tree**, in a directory of your own. In
the examples, `$RS` is the repository checkout and `$ATN` the
AcquaThermoNet one:

```sh
RS=/home/devel/Sviluppi/Pers/Thermonator
ATN=/home/devel/Sviluppi/Pers/HA_Hmi_Relay
```

---

## 1. Once, after the clone

```sh
cd $RS
git submodule update --init
```

The submodules are QtMqtt (`ThirdParty/qtmqtt`, v5.13.2, static, always)
and BlueZ (`ThirdParty/bluez`, 5.50: libbluetooth static and the
`hciconfig` tool, ARM only). The first build compiles them; they are
rebuilt only when missing (after changing a submodule delete
`<build>/ThirdParty`) or when the compiler options change.

---

## 2. Targets

| Target | Toolchain | BLE | Output |
|---|---|---|---|
| Desktop x86_64 (reference) | Qt 5.13.2, `/home/devel/Sviluppi/Qt/5.13.2/bin/qmake` | generated readings | `bin_x86_64/RoomSense` |
| Desktop x86_64 (optional) | Qt 5.15.x qmake | generated readings | `bin_x86_64/RoomSense` |
| House panel UN65 (Cortex-A9) | SDK `/home/devel/Sviluppi/Sdk/1.3.x` (Cortex-A7) + `-mcpu=cortex-a9` | real HCI scan | `bin_arm/RoomSense`, `hciconfig` + package zip in `dist/` |

> The SDK compiles for the Cortex-A7, the house panel is a **Cortex-A9**
> without hardware integer division: `ThirdParty/thirdparty.pri` adds
> `-mcpu=$$RS_ARM_CPU` (default `cortex-a9`) to our code and to the static
> libraries. Check: `objdump -d bin_arm/RoomSense | grep -cE '(s|u)div'`
> must print 0, or the panel stops with "Illegal instruction". Another
> panel: `qmake RS_ARM_CPU=<cpu>`.

### 2.1 Desktop

```sh
mkdir -p ~/build/rs-desktop && cd ~/build/rs-desktop
/home/devel/Sviluppi/Qt/5.13.2/bin/qmake -r $RS/RoomSense.pro
make -j$(nproc)
./bin_x86_64/RoomSense --version
```

To run it: from a directory with a `setting.ini` (created from the
template copied next to the binary at the first start). The desktop
build generates the readings of the `[SENSORS]`; events with `RS_FAKE`:

```sh
cd ~/build/rs-desktop/bin_x86_64
RS_FAKE="spike=0@4:+80;lost=1@20;press=0@10:+2;mode=window@30" ./RoomSense
```

Syntax in `fakescenario.h`: `lost`, `back`, `spike`, `out`, `step`,
`cold`, `nobatt`, `press` (panel presses), `mode` (house mode); the
sensor index is the `[SENSORS]` group order, alphabetical.

**Never on the real broker with the real zone names**: a live
AcquaThermoNet would take the generated readings and commands. Use a
local broker (e.g. `$ATN/tools/minibroker.py 1883 broker.log`) or the
test zones of `tools/broker_check.sh`.

HTTPS (weather) with the Qt 5.13 desktop needs an OpenSSL 1.1, e.g.
the one built by AcquaThermoNet: `LD_PRELOAD="<atn build>/ThirdParty/openssl/libcrypto.so <atn build>/ThirdParty/openssl/libssl.so"`.

### 2.2 House panel

```sh
source /home/devel/Sviluppi/Sdk/1.3.x/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
mkdir -p ~/build/rs-arm && cd ~/build/rs-arm
qmake -r $RS/RoomSense.pro
make -j$(nproc)
ls dist/
```

Every ARM link also makes the package zip (§4). A Qt Creator kit without
the SDK environment variables is handled (archiver, no FORTIFY in -O0
debug builds).

### 2.3 qmake options

| Option | Effect |
|---|---|
| `CONFIG+=tests` | also build the unit tests (`tests/tests`) |
| `CONFIG+=package` | desktop: make the package zip too |
| `CONFIG+=nopackage` | ARM: no package zip |
| `RS_ARM_CPU=<cpu>` | ARM: CPU of the panel (default `cortex-a9`) |

`make install`: desktop to `/tmp`, ARM to `/mnt/data/hmi/RoomSense`
(binary and `setting.default.ini`; `setting.ini` is never overwritten).

### 2.4 Version

From the latest git tag `vX.Y.Z` **when qmake runs** (`version.pri`):
`3.1.0` at the tag, `3.1.0-<hash>` after it, `0.0.0` with no tag
reachable. The release tags live on the `public` branch, so a build of
`master` reports `0.0.0-<hash>`; a release is built from its tag. Rerun
`qmake -r` after a commit or a tag (a plain qmake on the subdirs project
keeps the old `version.h`).

---

## 3. Tests

What each test checks: [`TESTING.md`](TESTING.md).

### 3.1 Unit tests

No BLE, broker or display needed (desktop build, offscreen by default):

```sh
mkdir -p ~/build/rs-tests && cd ~/build/rs-tests
/home/devel/Sviluppi/Qt/5.13.2/bin/qmake -r $RS/RoomSense.pro CONFIG+=tests
make -j$(nproc)
./tests/tests
```

All the suites run in one go; each prints a `Totals:` line and the last
line is `OK: 0 failed test function(s)` (exit status 0) or `FAIL: …`.
Quick check: `./tests/tests 2>&1 | grep -E '^Totals|FAIL|OK:'`.

### 3.2 End to end, with AcquaThermoNet

RoomSense and the real AcquaThermoNet (desktop builds), through the
broker and relay board simulators of AcquaThermoNet, about two minutes:

```sh
$RS/tools/e2e.sh ~/build/rs-tests/bin_x86_64/RoomSense \
    <atn build>/bin_x86_64/AcquaThermoNet $ATN
```

It runs in its own network namespace (`unshare -rn`, no root): it cannot
reach a real broker and does not disturb instances that are running.

### 3.3 Against the real broker

Only on test zones (`e2e_test_1`, `e2e_test_2`) that AcquaThermoNet
ignores; the password from the environment, never on the command line:

```sh
MQTT_PASSWORD=... $RS/tools/broker_check.sh ~/build/rs-desktop/bin_x86_64/RoomSense <host> 1883 <user>
```

No other RoomSense may run on the PC (single instance).

---

## 4. Package zip for the house panel

### 4.1 From a build

The ARM build (§2.2) makes, after every link:

```
dist/RoomSense_Package_Arm32_<version>.zip     installed by JMLauncher
dist/RoomSense_Arm32_<version>.debug           not stripped, keep it for core dumps
```

The binaries in the zip (`RoomSense`, `hciconfig`) are stripped. A
`make` that relinks nothing makes no new zip. By hand, SDK environment
loaded:

```sh
$RS/tools/package/make_package.sh --bin-dir ~/build/rs-arm/bin_arm --arch Arm32 \
    --strip arm-poky-linux-gnueabi-strip
```

### 4.2 Release package

From the release **tag** (on `public`), in a clean checkout:

```sh
git -C $RS worktree add /tmp/rs-v3.1.0 v3.1.0
cd /tmp/rs-v3.1.0 && git submodule update --init
source /home/devel/Sviluppi/Sdk/1.3.x/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
mkdir build && cd build && qmake -r ../RoomSense.pro && make -j$(nproc)
```

Before installing, check:

| Check | Command | Expected |
|---|---|---|
| version | `unzip -p dist/*.zip package.info` | `<version>3.1.0</version>`, no hash |
| no hardware division | `objdump -d bin_arm/RoomSense \| grep -cE '(s\|u)div'` | `0` |
| content | `unzip -l dist/*.zip` | `deploy/RoomSense`, `deploy/hciconfig`, `deploy/start.sh`, `deploy/setting.default.ini`, the hooks |
| checksum | `(cd dist && sha256sum *.zip > RoomSense_Package_Arm32_3.1.0.zip.sha256)` | kept with the zip |

Then keep the zip with its `.debug` and remove the worktree
(`git -C $RS worktree remove /tmp/rs-v3.1.0`).

### 4.3 Install

Copy the zip to the house panel and install it from the launcher. An
update keeps `setting.ini` and `log/`. On a first install the application
creates `setting.ini` from `setting.default.ini`: edit it (broker, sensor
MACs and zones, screen saver, weather) and restart. RoomSense and
AcquaThermoNet must be updated together when the interface changes.
