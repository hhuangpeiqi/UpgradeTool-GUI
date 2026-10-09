# UpgradeTool-GUI

Rockchip 设备固件烧录工具，基于 Qt6 Quick/QML，支持 Loader 和 Maskrom 模式。

![浅色主题](docs/gui.png)
![深色主题](docs/gui-dark.png)

## 功能

- 整包固件升级、信息解析与解包。
- Boot 下载、Loader 升级，按 `parameter.txt` 分区烧录镜像。
- 设备、Flash、芯片、OTP、安全模式及分区表查询。
- LBA 读写、扇区写入、块擦除、镜像导出。
- 序列号读写、串口日志导出、GPT 生成。
- 设备自动扫描、批量烧录、任务进度、取消与日志。

## 使用

连接处于 Loader 或 Maskrom 模式的设备，在底部设备栏选择目标。

| 页面 | 操作 |
|---|---|
| 下载镜像 | 选择包含 `parameter.txt` 和镜像的目录，勾选分区后执行。自动匹配分区名与镜像文件，双击路径可手动指定。目录中的 Loader 显示在首行。 |
| 升级固件 | 选择固件，确认芯片、版本等信息后升级。 |
| 高级功能 | 查询设备信息、读写序列号、导出日志或镜像、擦除存储。 |
| 配置 | 编辑配置、生成 GPT、安装 Linux 设备规则、切换主题。 |

启动参数：

```sh
./build/upgrade_tool_gui --firmware /path/to/update.img
./build/upgrade_tool_gui --tab 0 --image-dir /path/to/images
```

`--tab`：`0` 下载镜像、`1` 升级固件（默认）、`2` 高级功能。

### 设备权限

**Linux**：在「配置」中安装 udev 规则，或执行：

```sh
sudo scripts/install-udev.sh
```

`.deb` 安装时会自动安装规则。设备栏显示 `Vid=0x2207,Pid=0x... Mode=Loader|Maskrom` 时，已识别设备。

**macOS**：无需另装 USB 驱动。可选安装 ADB，用于将 Android 设备切换到 Loader。

**Windows**：使用 [Zadig](https://zadig.akeo.ie/) 为 VID `2207` 的设备安装 WinUSB 驱动。尚未在真实 Windows 环境验证。

## 构建

依赖 CMake 3.21.1+、C++11 编译器、Qt6（Core、Gui、Quick、QuickControls2）。libusb 通过子模块静态链接，无需单独安装。

首次构建前拉取子模块：

```sh
git submodule update --init --recursive
```

### Linux

Debian / Ubuntu：

```sh
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-templates qml6-module-qtquick-window \
  qml6-module-qtqml-workerscript qt6-qpa-plugins
```

SVG 插件按发行版安装 `qt6-svg-plugins` 或 `libqt6svg6`。

```sh
cmake -S . -B build
cmake --build build -j
./build/upgrade_tool_gui
```

### macOS（Apple Silicon）

最低部署目标为 macOS 12，已在 Qt 6.8.3、arm64 环境验证。将 `CMAKE_PREFIX_PATH` 替换为本机 Qt 路径：

```sh
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos" \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-macos --parallel
open build-macos/upgrade_tool_gui.app
```

### Windows

需安装 Qt6 和 MSVC / MinGW 工具链；构建流程尚未验证。

### 测试与打包

```sh
cmake -S . -B build -DUPGRADE_TOOL_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# Linux：通过 Docker 构建 .deb，支持 Ubuntu 22.04 及更新版本
scripts/package_deb.sh

# macOS：生成独立应用和 DMG，产物位于 build-macos/dist/
QT_ROOT="$HOME/Qt/6.8.3/macos" scripts/package_macos.sh
```

macOS 打包需系统支持 `diskutil image create/attach`；应用使用 ad-hoc 签名，未经开发者签名或公证。

## 配置与日志

`config.ini` 查找顺序：用户配置目录 → 可执行文件目录 → macOS 应用的 `Contents/Resources/`。在「配置」中保存后写入用户配置并立即生效。

Linux 默认路径：

- 配置：`~/.config/upgrade_tool/config.ini`，遵循 `XDG_CONFIG_HOME`。
- 日志：`~/.local/state/upgrade_tool/log/`，遵循 `XDG_STATE_HOME`。

配置格式为 `键=值`，以 `#` 开头的行为注释。仓库中的配置全部注释，使用默认行为。

| 键 | 含义 |
|---|---|
| `lba_parity` | LBA 传输奇偶校验 |
| `rb_check_off` | 关闭写入回读校验 |
| `nor_single_idb` | NOR 单份 IDB |
| `log_off` | 关闭日志 |
| `stdout_buffer_off` | 关闭 stdout 缓冲 |
| `force_data_band` | 强制数据带宽 |
| `log_dir` | 日志目录 |
| `firmware` | 未保存最近固件时的默认路径 |
| `loader` | 操作未指定 Loader 时的默认路径 |
| `device` | 兼容保留，目标设备在设备栏选择 |

## 许可

协议与设备核心派生自 `rockchip-linux/rkdeveloptool`，遵循 GPL-2.0，见 [lib/license.txt](lib/license.txt)。内置 libusb 遵循 LGPL-2.1。
