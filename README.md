# UpgradeTool-GUI

瑞芯微与全志设备固件刷写工具，基于 Qt6 Quick/QML。瑞芯微支持 Loader / Maskrom，全志支持 FEL / FES。

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

在顶部选择厂商，连接处于刷机模式的设备，在底部设备栏选择目标。批量刷写限同一厂商，按设备顺序执行。

| 页面 | 操作 |
|---|---|
| 下载镜像 | 选择包含 `parameter.txt` 和镜像的目录，勾选分区后执行。自动匹配分区名与镜像文件，双击路径可手动指定。目录中的 Loader 显示在首行。 |
| 升级固件 | 选择固件，确认芯片、版本等信息后升级。 |
| 高级功能 | 查询设备信息、读写序列号、导出日志或镜像、擦除存储。 |
| 配置 | 编辑配置、生成 GPT、安装 Linux 设备规则、切换主题。 |

全志工作区分为裸镜像刷写、固件刷写和高级功能；高级功能支持离线查看、解包、程序器镜像转换，以及在 FES 模式下重启设备。设备信息位于“设备”菜单。支持未加密 IMAGEWTY v1/v2/v3 的信息解析与解包，以及分区、保留用户数据、分区擦除和全擦四种刷写模式。默认全擦、开启校验、完成重启；校验失败会停止任务。按所选分区刷写仍会更新分区表和引导组件。

全志裸镜像与固件刷写页支持通过 ADB“切换”到 FEL，默认自动选择标准 `efex` 入口；识别到 T3 Buildroot（且具备 root 与 `devmem`）时自动改用兼容入口，也可手选“备选”强制指定。切换后检测目标 USB 端口确认结果，不会自动开始刷写。

裸镜像刷写需要匹配板型的 IMAGEWTY 引导固件，固定全擦、不校验、完成重启。镜像转换支持 SPI NOR、SD 卡、eMMC、SD NAND、UFS，可自动读取 DTB 偏移与安全启动标记；无需连接设备。

启动参数：

```sh
./build/upgrade_tool_gui --firmware /path/to/update.img
./build/upgrade_tool_gui --tab 0 --image-dir /path/to/images
./build/upgrade_tool_gui --vendor allwinner --firmware /path/to/allwinner.img
```

`--vendor` 默认为 `rockchip`，可选 `allwinner`。瑞芯微 `--tab`：`0` 下载镜像、`1` 升级固件（默认）、`2` 高级功能；全志依次为裸镜像刷写、固件刷写、高级功能；`--firmware` 载入所选页面的固件包。

### 设备权限

**Linux**：在「配置」中安装 udev 规则，或执行：

```sh
sudo scripts/install-udev.sh
```

`.deb` 安装时会自动安装两种厂商规则。全志 USB ID 为 `1f3a:efe8`；如提示权限错误，安装规则后重新连接设备。设备栏显示 `Vid=0x2207,Pid=0x... Mode=Loader|Maskrom` 时，已识别设备。

**macOS**：无需另装 USB 驱动。可选安装 ADB，用于将 Android 设备切换到 Loader。

**Windows**：使用 [Zadig](https://zadig.akeo.ie/) 为瑞芯微 VID `2207` 或全志 `1f3a:efe8` 设备安装 WinUSB 驱动。尚未在真实 Windows 环境验证。

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

最低部署目标为 macOS 12，原瑞芯微功能已在 Qt 6.8.3、arm64 环境验证；新增全志功能尚未在 macOS 验证。将 `CMAKE_PREFIX_PATH` 替换为本机 Qt 路径：

```sh
cmake -S . -B build-macos -G Ninja \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos" \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-macos --parallel
open build-macos/upgrade_tool_gui.app
```

### Windows

使用 Qt6 MinGW 工具链、CMake 和 Ninja，在对应 Qt MinGW 环境的 PowerShell 中运行：

```powershell
./scripts/package_windows.ps1 -QtRoot C:/Qt/6.8.3/mingw_64
```

产物位于 `build-windows/dist/UpgradeTool-GUI/`。Windows 构建和实机尚未验证。

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

协议与设备核心派生自 `rockchip-linux/rkdeveloptool`，遵循 GPL-2.0，见 [lib/license.txt](lib/license.txt)。内置 libusb 遵循 LGPL-2.1。全志功能参考 OpenixCLI，协议核心来自 libefex，均遵循 MIT，来源与许可见 [core/allwinner/README.md](core/allwinner/README.md) 和 [third_party/libefex/README.md](third_party/libefex/README.md)。
