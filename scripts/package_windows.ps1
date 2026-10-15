param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$BuildDir = "build-windows"
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = [IO.Path]::GetFullPath((Join-Path $Root $BuildDir))
function Run-Checked([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" }
}
if (-not (Test-Path (Join-Path $QtRoot "lib/cmake/Qt6SerialPort/Qt6SerialPortConfig.cmake"))) {
    throw "Qt SerialPort is required; install it for the selected Qt toolchain."
}
# Run from the matching Qt MinGW shell; the Rockchip core uses POSIX CRT APIs.
Run-Checked { cmake -S $Root -B $Build -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" -DUPGRADE_TOOL_TESTS=ON -DCMAKE_BUILD_TYPE=Release }
Run-Checked { cmake --build $Build --parallel }
Run-Checked { ctest --test-dir $Build --output-on-failure }
$Stage = Join-Path $Build "dist/UpgradeTool-GUI"
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item (Join-Path $Build "upgrade_tool_gui.exe") $Stage
Copy-Item (Join-Path $Root "config.ini") $Stage
Run-Checked { & (Join-Path $QtRoot "bin/windeployqt.exe") --release --compiler-runtime --qmldir (Join-Path $Root "gui/qml") (Join-Path $Stage "upgrade_tool_gui.exe") }
$RuntimeSearch = ($env:PATH -split ';') -join ';'
Run-Checked { cmake "-DAPP=$(Join-Path $Stage 'upgrade_tool_gui.exe')" "-DDESTINATION=$Stage" "-DSEARCH_DIRS=$RuntimeSearch" -P (Join-Path $Root "scripts/collect_windows_runtime.cmake") }
$Licenses = Join-Path $Stage "licenses"
New-Item -ItemType Directory -Force $Licenses | Out-Null
Copy-Item (Join-Path $Root "backends/rockchip/legacy/license.txt") (Join-Path $Licenses "upgrade-tool-GPL-2.0.txt")
Copy-Item (Join-Path $Root "third_party/libusb/COPYING") (Join-Path $Licenses "libusb-LGPL-2.1.txt")
Copy-Item (Join-Path $Root "third_party/libefex/LICENSE") (Join-Path $Licenses "libefex-MIT.txt")
Copy-Item (Join-Path $Root "backends/allwinner/LICENSE.OpenixCLI") (Join-Path $Licenses "OpenixCLI-MIT.txt")
Copy-Item (Join-Path $Root "third_party/qdl/LICENSE") (Join-Path $Licenses "qdl-BSD-3-Clause.txt")
Write-Output "Application: $Stage"
