# UpgradeTool-GUI

**English** | [中文](README.zh-CN.md)

Firmware flashing tool for Rockchip (Loader / Maskrom) and Allwinner (FEL / FES) devices, built with Qt6 Quick/QML.

![Light theme](docs/gui.png)
![Dark theme](docs/gui-dark.png)

## Features

- Full firmware upgrade, info parsing, and unpacking.
- Boot download and loader upgrade, flashing images by `parameter.txt` partitions.
- Device, flash, chip, OTP, secure-mode, and partition-table queries.
- LBA read/write, sector write, block erase, image export.
- Serial number read/write, serial log export, GPT generation.
- Automatic device scan, batch flashing, task progress, cancel, and logging.

## Usage

Pick a vendor, connect a device in flashing mode, then select the target in the bottom device bar. Batch flashing is limited to one vendor and runs in device order. The UI language follows the system by default; switch between Chinese and English in **Config → Preferences…**.

| Page | Action |
|---|---|
| Download Image | Select a directory containing `parameter.txt` and images, tick partitions, then run. Partition names are matched to image files; double-click a path to pick one manually. A Loader in the directory is shown in the first row. |
| Upgrade Firmware | Select a firmware package and confirm chip, version, and other info before upgrading. |
| Advanced | Query device info, read/write serial numbers, export logs or images, erase storage. |
| Config | Edit config, generate GPT, install Linux device rules, switch theme. |

Allwinner workspaces are bare-image flashing, firmware flashing, and advanced features. Advanced supports offline inspection, unpacking, programmer image conversion, and rebooting the device in FES mode; device info is under the **Device** menu. Unencrypted IMAGEWTY v1/v2/v3 parsing and unpacking are supported, with four flash modes: partition, keep user data, erase partition, and full erase (defaults: full erase, verification on, reboot when done; a failed verification stops the task).

Allwinner bare-image and firmware pages can switch to FEL over ADB. The standard `efex` entry is used by default, a compatible T3 Buildroot entry is auto-detected, or you can force the alternative. After switching, the target USB port is checked to confirm the result; flashing does not start automatically. Bare-image flashing needs a board-matched IMAGEWTY boot firmware and always uses full erase, no verification, reboot when done. Image conversion supports SPI NOR, SD card, eMMC, SD NAND, and UFS, reads the DTB offset and secure-boot flag automatically, and needs no device.

Startup arguments:

```sh
./build/upgrade_tool_gui --firmware /path/to/update.img
./build/upgrade_tool_gui --tab 0 --image-dir /path/to/images
./build/upgrade_tool_gui --vendor allwinner --firmware /path/to/allwinner.img
```

`--vendor` defaults to `rockchip`; the other value is `allwinner`. Rockchip `--tab`: `0` Download Image, `1` Upgrade Firmware (default), `2` Advanced; Allwinner uses bare-image flashing, firmware flashing, and advanced features in that order. `--firmware` loads a firmware package into the selected page. `--lang` defaults to `auto` (follow the system) and also accepts `zh` or `en`.

### Device permissions

**Linux**: Install the udev rules from **Config**, or run:

```sh
sudo scripts/install-udev.sh
```

The `.deb` installs both vendor rule sets automatically. The Allwinner USB ID is `1f3a:efe8`; if you see a permission error, install the rules and reconnect the device. A device is recognized when the device bar shows `Vid=0x2207,Pid=0x... Mode=Loader|Maskrom`.

**macOS**: No extra USB driver is needed. ADB is optional, to switch an Android device to Loader.

**Windows**: Use [Zadig](https://zadig.akeo.ie/) to install the WinUSB driver for Rockchip VID `2207` or Allwinner `1f3a:efe8` devices. Not yet verified on real Windows.

## Build

Requires CMake 3.21.1+, a C++11 compiler, and Qt6 (Core, Gui, Quick, QuickControls2). libusb is statically linked through a submodule; no separate install is needed.

Fetch submodules before the first build:

```sh
git submodule update --init --recursive
```

### Linux

Debian / Ubuntu:

```sh
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev qt6-l10n-tools qt6-tools-dev-tools \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript qt6-qpa-plugins
```

Install the SVG plugin as `qt6-svg-plugins` or `libqt6svg6`, depending on the distribution.

```sh
cmake -S . -B build
cmake --build build -j
./build/upgrade_tool_gui
```

### macOS (Apple Silicon)

The minimum deployment target is macOS 12; the original Rockchip features are verified on Qt 6.8.3, arm64. Replace `CMAKE_PREFIX_PATH` with your local Qt path:

```sh
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos" \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-macos --parallel
open build-macos/upgrade_tool_gui.app
```

### Windows

Using the Qt6 MinGW toolchain, CMake, and Ninja, run in the PowerShell of the matching Qt MinGW environment:

```powershell
./scripts/package_windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64
```

The output is in `build-windows/dist/UpgradeTool-GUI/`. Windows build and hardware are not yet verified.

### Tests and packaging

```sh
cmake -S . -B build -DUPGRADE_TOOL_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# Linux: build a .deb via Docker, for Ubuntu 22.04 and newer
scripts/package_deb.sh

# macOS: build a standalone app and DMG; output in build-macos/dist/
QT_ROOT="$HOME/Qt/6.8.3/macos" scripts/package_macos.sh
```

macOS packaging requires system support for `diskutil image create/attach`; the app is ad-hoc signed, without developer signing or notarization.

## Configuration and logs

`config.ini` is searched in order: user config directory → executable directory → the macOS app's `Contents/Resources/`. Saving in **Config** writes to the user config and takes effect immediately.

Default Linux paths:

- Config: `~/.config/upgrade_tool/config.ini`, following `XDG_CONFIG_HOME`.
- Logs: `~/.local/state/upgrade_tool/log/`, following `XDG_STATE_HOME`.

The config format is `key=value`, with lines starting with `#` as comments. The repository config is fully commented and uses default behavior.

| Key | Meaning |
|---|---|
| `lba_parity` | LBA transfer parity check |
| `rb_check_off` | Disable write read-back verification |
| `nor_single_idb` | Single IDB for NOR |
| `log_off` | Disable logging |
| `stdout_buffer_off` | Disable stdout buffering |
| `force_data_band` | Force data bandwidth |
| `log_dir` | Log directory |
| `firmware` | Default path when no recent firmware is saved |
| `loader` | Default path when no loader is specified for an operation |
| `device` | Kept for compatibility; the target device is chosen in the device bar |

## License

The protocol and device core are derived from `rockchip-linux/rkdeveloptool` under GPL-2.0; see [lib/license.txt](lib/license.txt). The bundled libusb is under LGPL-2.1. Allwinner support references OpenixCLI, with the protocol core from libefex, both under MIT; see [core/allwinner/README.md](core/allwinner/README.md) and [third_party/libefex/README.md](third_party/libefex/README.md).
