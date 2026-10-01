# RoomSense deployment package

`make_package.sh` builds the zip installed on the device by its launcher
(JMLauncher): a `package.info` descriptor, the lifecycle hooks and the
payload. Same layout and hooks as the AcquaThermoNet package
(`HA_Hmi_Relay/tools/package`): application and `start.sh` in `deploy/`.
No library is bundled: QtMqtt and libbluetooth are linked statically
(built from the `ThirdParty` submodules), the rest of Qt comes from the
device.

The house panel is a **UN65, Cortex-A9**: built with the SDK
`Sdk/1.3.x` (Cortex-A7) and `-mcpu=cortex-a9` (`RS_ARM_CPU`,
`ThirdParty/thirdparty.pri`), so that no hardware division (`sdiv`/`udiv`,
"Illegal instruction" on the A9) is generated.

## Built with every build

`app/app.pro` runs `make_package.sh` after every link of an ARM build
(Arm32, Arm64), so building in Qt Creator or with `make` is enough:

```
<build dir>/dist/RoomSense_Package_Arm32_<version>.zip
<build dir>/dist/RoomSense_Arm32_<version>.debug
```

- desktop builds: only with `CONFIG+=package` (qmake argument);
- ARM builds without the package: `CONFIG+=nopackage`;
- it runs only when the binary is relinked, not on a no-op `make`.

Debug information: the binaries in the zip are **stripped** (the SDK
compiles with `-g` also in release). The full binary is kept next to the
zip as `RoomSense_<arch>_<version>.debug`: keep it with the release to
read a core dump of the device (`gdb RoomSense_Arm32_<version>.debug
core`; `start.sh` enables core dumps). The strip is the one of the build
toolchain (`$(STRIP)` of the Makefile, searched next to the compiler when
the SDK environment is not loaded, as in Qt Creator); a missing or wrong
strip stops the packaging with an error. `--no-strip` packages the full
binary.

Version: the latest git tag `vX.Y.Z` (`0.0.0` before the first one or
outside a git checkout); built after the tag, the commit short hash is
appended (`1.0.0-0566e34`); `--version` overrides it. By hand:

```
tools/package/make_package.sh --bin-dir <build>/bin_arm --arch Arm32 \
    --strip arm-poky-linux-gnueabi-strip      # SDK environment loaded
```

## Content

```
package.info                name, version, installationFolder, executeAsRoot=true, background=false
install.sh uninstall.sh update.sh run.sh stop.sh
deploy/
    RoomSense               binary, stripped (QtMqtt, libbluetooth linked statically)
    hciconfig               BlueZ 5.50 HCI tool, stripped (ThirdParty/hciconfig.pro)
    setting.default.ini     configuration template
    start.sh                Bluetooth reset, environment and restart loop
```

`deploy/` is also the working directory of the application: `setting.ini`
and `log/` are created there.

- **run.sh**: entry point called by the launcher. Closes the boot splash
  (`killall xsplash`, as the reference packages of the panel), runs `deploy/start.sh` in a
  subshell, keeps `/var/run/roomsense-run.pid`, sends the
  `appLoaded`/`appFinished` dbus notifications.
- **start.sh**:
  - **Bluetooth reset** before the first start: `hciconfig hci0 down`,
    the RSI module switched off and on through GPIO 92 with the
    `rsi_usb`/`rsi_91x` drivers reloaded (`dev_oper_mode=13`, BLE), wait
    for `hci0` (max 10s), `hciconfig hci0 up`. Every step is logged to
    syslog (tag `RoomSense`); a failure does not stop the start (the
    application keeps retrying the adapter). Skipped without GPIO 92 or
    `hciconfig`. Customize it in `template/start.sh` (`bluetooth_reset`).
  - environment (DISPLAY, xcb, Qt plugins, `lib/` in `LD_LIBRARY_PATH`)
    and a restart loop. Exit status of the application:
    - `0`: stopped on purpose (SIGTERM/SIGINT, MQTT offline): loop ends;
    - `3`: another instance already runs on the device: loop ends;
    - anything else or a signal: crash, restarted after 2, 4, 8, 16, 20s
      (back to 2s after a run of at least a minute): AcquaThermoNet closes
      a zone after 15 min without readings.
- **stop.sh**: SIGTERM to the application and wait for the clean shutdown
  (BLE scan stopped, MQTT offline); killed after 15s.
- **install.sh**: restore of the files saved by `uninstall.sh` into
  `deploy/`, `showProgress`/`installFinished` dbus notifications (and
  soname symlinks for libraries in `deploy/lib/`, if a package ever
  bundles some with `--lib-dir`).
- **uninstall.sh**: stops the application and saves `deploy/setting.ini`
  and `deploy/log/` to `/tmp/roomsense-backup`. The launcher's update is
  uninstall + install, so this is what keeps the device configuration
  (broker, sensors, zones) and the log across an update.
- **update.sh**: nothing to do, `updateFinished` notification.

On a fresh install there is no `setting.ini`: the application creates it
from `setting.default.ini` at the first start. Edit it for the device
(broker, sensor MACs and zones) and restart.

`executeAsRoot=true`: the BLE scan uses a raw HCI socket, the watchdog
`/dev/watchdog`, the Bluetooth reset GPIO 92 and the kernel modules.

## To verify on the device

- `background=false`: RoomSense is the foreground, full screen
  application of its own panel in the house (AcquaThermoNet runs on
  another device, in the boiler room);
- `installationFolder` is `RoomSense`;
- the `appLoaded` check waits 5s for the process `RoomSense`.
