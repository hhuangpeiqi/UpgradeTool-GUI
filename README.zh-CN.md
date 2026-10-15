# UpgradeTool-GUI

[English](README.md) | **中文**

瑞芯微（Loader / Maskrom）、全志（FEL / FES）及高通（EDL）设备固件刷写工具，基于 Qt6 Quick/QML。

![浅色主题](docs/gui.png)
![深色主题](docs/gui-dark.png)

## 支持的设备

| 厂商 | 刷写模式 | 页面（1 / 2 / 3） | USB ID |
|---|---|---|---|
| 瑞芯微 | Loader、Maskrom | 下载镜像 / 升级固件 / 高级功能 | VID `2207` |
| 全志 | FEL、FES | 裸镜像刷写 / 固件刷写 / 高级功能 | `1f3a:efe8` |
| 高通 | EDL（9008） | 分区工具 / 下载 / 工具 | VID `05c6` |

## 功能

各厂商通用：

- 设备自动扫描、批量刷写（每批限同一厂商，按设备顺序执行）、任务进度、取消与日志。
- 中英文界面、浅色与深色主题。

**瑞芯微**

- 整包固件升级、信息解析与解包。
- Boot 下载、Loader 升级，按 `parameter.txt` 分区烧录镜像。
- 设备、Flash、芯片、OTP、安全模式及分区表查询。
- LBA 读写、扇区写入、块擦除、镜像导出。
- 序列号读写、串口日志导出、GPT 生成。

**全志**

- 未加密 IMAGEWTY v1/v2/v3 的信息解析与解包。
- 裸镜像与固件刷写，含分区、保留用户数据、分区擦除、全擦四种模式。
- 在刷写页可通过 ADB 切换到 FEL。
- 离线程序器镜像转换，支持 SPI NOR、SD 卡、eMMC、SD NAND、UFS。
- 设备信息查询，以及在 FES 模式下重启设备。

**高通**

- 基于 qdl 的 Sahara 与 Firehose 协议，无需单独的 qdl 可执行文件。
- 使用 qdl 地址的分区工具，以及由 rawprogram 与 patch XML 驱动的 Flat Build 下载。
- 支持从 DIAG 诊断口或已授权的 ADB 设备切换到 EDL（9008）。
- Dry run 模拟执行、UFS 配置及打包（zip、整包、摘要）。

## 使用

在顶部选择厂商，连接处于刷机模式的设备，在底部设备栏选择目标。界面语言默认跟随系统，可在「配置 → 首选项…」切换中英文。

### 瑞芯微

| 页面 | 操作 |
|---|---|
| 下载镜像 | 选择包含 `parameter.txt` 和镜像的目录，勾选分区后执行。分区名自动匹配镜像文件，双击路径可手动指定；目录中的 Loader 显示在首行。 |
| 升级固件 | 选择固件，确认芯片、版本等信息后升级。 |
| 高级功能 | 查询设备信息、读写序列号、导出日志或镜像、擦除存储。 |
| 配置 | 编辑配置、生成 GPT、安装 Linux 设备规则、切换主题。 |

### 全志

| 页面 | 操作 |
|---|---|
| 裸镜像刷写 | 选择匹配板型的 IMAGEWTY 引导固件与待写入镜像，可选写入方式（Command / 逻辑偏移）与逻辑偏移；固定全擦、不校验、完成重启。 |
| 固件刷写 | 选择固件包与刷写模式（分区 / 保留用户数据 / 分区擦除 / 全擦），可勾选写入后校验；默认全擦、开启校验、完成重启，校验失败停止任务。 |
| 高级功能 | 查看分区与内嵌文件、解包固件、在 FES 模式下重启设备、程序器镜像转换（SPI NOR / SD 卡 / eMMC / SD NAND / UFS）。 |
| 设备菜单 | 查询设备信息。 |

两个刷写页可通过 ADB 切换到 FEL：默认使用标准 `efex` 入口，识别到 T3 Buildroot 时自动改用兼容入口，也可强制指定备选入口。切换后检测目标 USB 端口确认结果，不会自动开始刷写。镜像转换可自动读取 DTB 偏移与安全启动标记，无需连接设备。

### 高通

| 页面 | 操作 |
|---|---|
| 分区工具 | 选择引导程序，按 qdl 地址（`LUN`、`LUN/sector`、`LUN/sector+count`、分区名或 `LUN/分区名`）添加读 / 写 / 擦除 / SHA256 / 复位操作并按顺序执行；显式 Firehose 复位须放在最后。 |
| 下载 | 选择构建类型（Flat Build / Meta）与引导程序，用「加载 XML」双击选择 rawprogram 与 patch（或「跳过 patch」）后执行下载；多 LUN 可用 Ctrl/Shift 点选，重新加载会覆盖上一次选择。 |
| 工具 | 芯片信息、复位、RAM dump、Sahara kickstart、创建 ZIP / Sahara 整包 / VIP 摘要，以及 UFS 配置。 |
| 切换设置… / 切换到 EDL | 选择设备、切换方式与诊断口（DIAG 或已授权 ADB），把设备切到 EDL（9008）模式。 |

「加载 XML」的对话框打开在引导程序所在目录，已加载列表为只读。诊断口不限于 9091，未知接口可手选并须通过只读 DIAG 查询。高通相关输入只存在于当前会话，不写入配置。勾选「Dry run（模拟执行）」可在不访问设备、也不写入读取输出文件的情况下模拟 qdl；但这不代表实机可用，仍需验证一次真实刷写与启动。切换只尝试一次，且只有设备在原 USB 位置重新枚举后才确认成功。

### 启动参数

```sh
./build/upgrade_tool_gui --firmware /path/to/update.img
./build/upgrade_tool_gui --tab 0 --image-dir /path/to/images
./build/upgrade_tool_gui --vendor allwinner --firmware /path/to/allwinner.img
```

`--vendor` 默认为 `rockchip`，可选 `allwinner`、`qualcomm`。`--tab` 默认为 `1`，按厂商顺序选择页面：

| `--vendor` | `--tab 0` | `--tab 1`（默认） | `--tab 2` |
|---|---|---|---|
| `rockchip` | 下载镜像 | 升级固件 | 高级功能 |
| `allwinner` | 裸镜像刷写 | 固件刷写 | 高级功能 |
| `qualcomm` | 分区工具 | 下载 | 工具 |

`--firmware` 载入所选页面的固件包。`--lang` 默认为 `auto`（跟随系统），可选 `zh`、`en`。

## 设备权限

**Linux**：在「配置」中安装 udev 规则，或执行：

```sh
sudo scripts/install-udev.sh
```

`.deb` 安装时会自动安装各厂商规则。如提示权限错误，安装规则后重新连接设备。设备栏显示 `Vid=0x2207,Pid=0x... Mode=Loader|Maskrom` 时，瑞芯微设备已识别。

**macOS**：无需另装 USB 驱动。可选安装 ADB，用于将 Android 设备切换到 Loader。

**Windows**：使用 [Zadig](https://zadig.akeo.ie/) 为瑞芯微 VID `2207` 或全志 `1f3a:efe8` 设备安装 WinUSB 驱动。高通另支持官方 QDLoader COM 驱动（QUD 后端）。尚未在真实 Windows 环境验证。

## 构建

依赖 CMake 3.21.1+、Git、GNU C11/C++20 编译器、Qt6（Core、Gui、Quick、QuickControls2、SerialPort），以及系统 zlib。libusb、libzip 和 libxml2 通过子模块静态链接。

首次构建前拉取子模块：

```sh
git submodule update --init --recursive
```

高通后端从 `third_party/qdl` 编译 qdl：CMake 会把上游源码拷入构建目录并应用 `cmake/patches/qdl-embedded.patch`（去掉 CLI 入口、加入回调与取消、替换 parser 的致命退出、为 USB 传输使用独立的 libusb context），无需额外的 qdl 可执行文件。更新子模块版本时，需同步 rebase 该 patch，并重跑测试与打包检查；patch 应用失败不得绕过。

使用 `-DUPGRADE_TOOL_GUI=OFF` 可构建不依赖 Qt 的核心。通过 `-DUPGRADE_TOOL_ROCKCHIP=OFF`、`-DUPGRADE_TOOL_ALLWINNER=OFF` 或 `-DUPGRADE_TOOL_QUALCOMM=OFF` 关闭指定厂商后端；GUI 至少需要一个后端。

### Linux

Debian / Ubuntu：

```sh
sudo apt install cmake git g++ zlib1g-dev qt6-base-dev qt6-serialport-dev qt6-declarative-dev qt6-l10n-tools qt6-tools-dev-tools \
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

最低部署目标为 macOS 12，原瑞芯微功能已在 Qt 6.8.3、arm64 环境验证。将 `CMAKE_PREFIX_PATH` 替换为本机 Qt 路径：

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

# Linux：用本机工具链构建 .deb，产物仅适用于当前发行版
UPGRADE_TOOL_NATIVE_DEB=1 scripts/package_deb.sh

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

| 组件 / 来源 | 许可 | 参考 |
|---|---|---|
| rkdeveloptool | GPL-2.0 | [backends/rockchip/legacy/license.txt](backends/rockchip/legacy/license.txt) |
| libusb | LGPL-2.1 | `third_party/libusb/COPYING` |
| OpenixCLI | MIT | [backends/allwinner/LICENSE.OpenixCLI](backends/allwinner/LICENSE.OpenixCLI) |
| libefex | MIT | [third_party/libefex/README.md](third_party/libefex/LICENSE) |
| qdl | BSD-3-Clause | [third_party/qdl/LICENSE](third_party/qdl/LICENSE) |
| libzip | BSD-3-Clause | `third_party/libzip/LICENSE` |
| libxml2 | MIT | `third_party/libxml2/Copyright` |

以各第三方子模块附带的许可文件与说明为准。
