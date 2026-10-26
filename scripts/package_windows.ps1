<#
.SYNOPSIS
Build, test and package using an installed Qt6 x64 MinGW kit.
.DESCRIPTION
Finds Qt, its matching compiler, CMake, Ninja, Git shell, zlib and Iconv without
changing persistent environment settings or installing dependencies.
.PARAMETER QtRoot
Optional Qt kit directory, for example C:/Qt/6.8.3/mingw_64.
.PARAMETER BuildDir
Build directory; relative paths are resolved from the repository root.
.PARAMETER ZlibRoot
Optional MinGW x64 zlib prefix containing include/zlib.h and lib/libz.a.
.PARAMETER CheckEnvironment
Report dependency paths and cache conflicts without creating build files.
.PARAMETER IconvRoot
Optional MinGW x64 Iconv prefix containing include/iconv.h and lib/libiconv.a.
#>
[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$BuildDir = "build-windows",
    [string]$ZlibRoot,
    [switch]$CheckEnvironment,
    [string]$IconvRoot
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = if ([IO.Path]::IsPathRooted($BuildDir)) { [IO.Path]::GetFullPath($BuildDir) } else { [IO.Path]::GetFullPath((Join-Path $Root $BuildDir)) }
function Run-Checked([scriptblock]$Command) {
    # Windows PowerShell can turn redirected native stderr warnings into errors.
    # Keep that output visible and use the native exit code to detect failures.
    $ErrorActionPreference = 'Continue'
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" }
}

function Read-Cache([string]$Name) {
    if (Test-Path -LiteralPath "$Build/CMakeCache.txt") {
        foreach ($line in Get-Content -LiteralPath "$Build/CMakeCache.txt") {
            if ($line -match ('^' + [regex]::Escape($Name) + ':[^=]+=(.*)$')) { return $Matches[1] }
        }
    }
}
function Full-Path([string]$Path) {
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path).TrimEnd('\', '/')
}
function Find-Tool([string]$Name, [string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) { return (Full-Path $candidate) }
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
}
function Is-Qt([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath "$Path/mkspecs/qconfig.pri")) { return $false }
    $config = Get-Content -LiteralPath "$Path/mkspecs/qconfig.pri" -Raw
    return ($config -match '(?m)^QT_MAJOR_VERSION\s*=\s*6\s*$' -and
        $config -match '(?m)^QT_ARCH\s*=\s*x86_64\s*$' -and
        (Test-Path -LiteralPath "$Path/mkspecs/win32-g++/qmake.conf") -and
        (Test-Path -LiteralPath "$Path/lib/libQt6Core.a"))
}

$savedEnvironment = @{}
foreach ($name in @('PATH', 'CC', 'CXX', 'CMAKE_PREFIX_PATH', 'Qt6_DIR', 'QTDIR', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $problems = [Collections.Generic.List[string]]::new()
    if (-not $QtRoot) {
        $cachedQt = Read-Cache 'CMAKE_PREFIX_PATH'
        if (Is-Qt $cachedQt) { $QtRoot = $cachedQt }
    }
    if (-not $QtRoot) {
        foreach ($candidate in @($env:QT_ROOT, $env:QTDIR, $env:CMAKE_PREFIX_PATH -split ';')) {
            if (Is-Qt $candidate) { $QtRoot = $candidate; break }
        }
    }
    if (-not $QtRoot) {
        $candidates = @(@(
            foreach ($base in @('C:/Qt', "$env:USERPROFILE/Qt")) {
                if (Test-Path -LiteralPath $base) {
                    Get-ChildItem -LiteralPath $base -Directory | ForEach-Object {
                        Get-ChildItem -LiteralPath $_.FullName -Directory | Where-Object { Is-Qt $_.FullName } | Select-Object -ExpandProperty FullName
                    }
                }
            }
            $qmake = Get-Command qmake.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($qmake) {
                $candidate = Split-Path (Split-Path $qmake.Source -Parent) -Parent
                if (Is-Qt $candidate) { $candidate }
            }
        ) | Sort-Object -Unique)
        if (@($candidates).Count -eq 1) { $QtRoot = $candidates[0] }
        elseif (@($candidates).Count -gt 1) { throw "Multiple Qt installations found. Select one with -QtRoot:`n$($candidates -join "`n")" }
        else { throw 'Qt6 x64 MinGW not found. Install it with Qt Maintenance Tool, or pass -QtRoot <Qt kit directory>.' }
    }
    $QtRoot = Full-Path $QtRoot
    if (-not (Is-Qt $QtRoot)) { throw "Not a Qt6 x64 MinGW kit: $QtRoot" }
    $qtInstall = Split-Path (Split-Path $QtRoot -Parent) -Parent
    foreach ($module in @('Core', 'Gui', 'Quick', 'QuickControls2', 'SerialPort', 'Test', 'QuickTest', 'LinguistTools')) {
        if (-not (Test-Path -LiteralPath "$QtRoot/lib/cmake/Qt6$module/Qt6${module}Config.cmake")) {
            $problems.Add("Missing Qt $module. Install it for $QtRoot with Qt Maintenance Tool.")
        }
    }
    $deploy = "$QtRoot/bin/windeployqt.exe"
    if (-not (Test-Path -LiteralPath "$env:SystemRoot/System32/makecab.exe")) { $problems.Add('Missing Windows makecab.exe, required for single-file packaging.') }
    if (-not (Test-Path -LiteralPath $deploy)) { $problems.Add("Missing Qt deployment tool: $deploy") }
    $cmake = Find-Tool 'cmake.exe' @("$qtInstall/Tools/CMake_64/bin/cmake.exe", "$qtInstall/Tools/CMake/bin/cmake.exe")
    $ninja = Find-Tool 'ninja.exe' @("$qtInstall/Tools/Ninja/ninja.exe")
    $git = Find-Tool 'git.exe' @()
    if (-not $git) {
        $git = Find-Tool 'git.exe' @("$env:ProgramFiles/Git/cmd/git.exe", "$env:LOCALAPPDATA/Programs/Git/cmd/git.exe")
    }
    $sh = $null
    if ($git) {
        $gitInstall = Split-Path (Split-Path $git -Parent) -Parent
        foreach ($candidate in @("$gitInstall/usr/bin/sh.exe", "$gitInstall/bin/sh.exe")) {
            if (Test-Path -LiteralPath $candidate) { $sh = $candidate; break }
        }
    }
    foreach ($tool in @(@('CMake', $cmake), @('Ninja', $ninja), @('Git for Windows', $git), @('Git for Windows sh', $sh))) {
        if (-not $tool[1]) { $problems.Add("Missing $($tool[0]). Install it using Qt Maintenance Tool or Git for Windows.") }
    }
    if ($cmake) {
        $cmakeVersion = @(& $cmake --version)
        if ($LASTEXITCODE -ne 0 -or $cmakeVersion[0] -notmatch 'cmake version (\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'3.21.1') {
            $problems.Add('CMake 3.21.1 or newer is required; update CMake using Qt Maintenance Tool.')
        }
    }
    $ctest = if ($cmake) { Join-Path (Split-Path $cmake -Parent) 'ctest.exe' }
    if (-not $ctest -or -not (Test-Path -LiteralPath $ctest)) { $problems.Add('Missing ctest.exe alongside CMake; install the complete CMake distribution.') }

    $qtConfig = Get-Content -LiteralPath "$QtRoot/mkspecs/qconfig.pri" -Raw
    $gccVersion = @(
        foreach ($part in @('MAJOR', 'MINOR', 'PATCH')) {
            if ($qtConfig -match "(?m)^QT_GCC_${part}_VERSION\s*=\s*(\d+)") { $Matches[1] }
        }
    ) -join '.'
    $mingwBins = @(
        # Read only the path hints; do not execute the vendor batch file.
        if (Test-Path -LiteralPath "$QtRoot/bin/qtenv2.bat") {
            $qtEnv = Get-Content -LiteralPath "$QtRoot/bin/qtenv2.bat" -Raw
            if ($qtEnv -match '(?im)^set\s+PATH=(.+)$') { $Matches[1].Trim() -split ';' | Where-Object { $_ -notmatch '%' } }
        }
        if (Test-Path -LiteralPath "$qtInstall/Tools") {
            Get-ChildItem -LiteralPath "$qtInstall/Tools" -Directory -Filter 'mingw*' | ForEach-Object { Join-Path $_.FullName 'bin' }
        }
        $gccCommand = Get-Command gcc.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($gccCommand) { Split-Path $gccCommand.Source -Parent }
    ) | Select-Object -Unique
    $mingw = $null
    foreach ($bin in $mingwBins) {
        if (-not (Test-Path -LiteralPath "$bin/gcc.exe") -or -not (Test-Path -LiteralPath "$bin/g++.exe")) { continue }
        $version = & "$bin/gcc.exe" -dumpfullversion
        if ($LASTEXITCODE -ne 0 -or $version -ne $gccVersion) { continue }
        $target = & "$bin/gcc.exe" -dumpmachine
        if ($LASTEXITCODE -ne 0 -or $target -ne 'x86_64-w64-mingw32') { continue }
        $cppVersion = & "$bin/g++.exe" -dumpfullversion
        if ($LASTEXITCODE -ne 0 -or $cppVersion -ne $gccVersion) { continue }
        $cppTarget = & "$bin/g++.exe" -dumpmachine
        if ($LASTEXITCODE -eq 0 -and $cppTarget -eq 'x86_64-w64-mingw32') { $mingw = Full-Path $bin; break }
    }
    if (-not $mingw) { $problems.Add("Missing matching MinGW x64 GCC $gccVersion. Install the compiler supplied for this Qt kit with Qt Maintenance Tool.") }
    elseif (-not (Test-Path -LiteralPath "$mingw/windres.exe")) { $problems.Add('Missing MinGW windres.exe, required for single-file packaging.') }

    $zlibInclude = $null
    $zlibLibrary = $null
    $zlibCandidates = if ($ZlibRoot) { @((Full-Path $ZlibRoot)) } elseif ($mingw) {
        @((Join-Path (Split-Path $mingw -Parent) 'x86_64-w64-mingw32'), (Split-Path $mingw -Parent))
    } else { @() }
    foreach ($candidate in $zlibCandidates) {
        if ((Test-Path -LiteralPath "$candidate/include/zlib.h") -and (Test-Path -LiteralPath "$candidate/include/zconf.h") -and (Test-Path -LiteralPath "$candidate/lib/libz.a")) {
            $ZlibRoot = $candidate
            $zlibInclude = "$candidate/include"
            $zlibLibrary = "$candidate/lib/libz.a"
            break
        }
    }
    if (-not $zlibLibrary) { $problems.Add('Missing MinGW x64 zlib headers/static library. Install zlib and pass -ZlibRoot <directory containing include/zlib.h and lib/libz.a>.') }
    $iconvInclude = $null
    $iconvLibrary = $null
    $iconvCandidates = if ($IconvRoot) { @((Full-Path $IconvRoot)) } elseif ($mingw) {
        @((Join-Path (Split-Path $mingw -Parent) 'x86_64-w64-mingw32'), (Split-Path $mingw -Parent))
    } else { @() }
    foreach ($candidate in $iconvCandidates) {
        if ((Test-Path -LiteralPath "$candidate/include/iconv.h" -PathType Leaf) -and (Test-Path -LiteralPath "$candidate/lib/libiconv.a" -PathType Leaf)) {
            $IconvRoot = $candidate
            $iconvInclude = "$candidate/include"
            $iconvLibrary = "$candidate/lib/libiconv.a"
            break
        }
    }
    if (-not $iconvLibrary) { $problems.Add('Missing MinGW x64 Iconv headers/static library. Install Iconv and pass -IconvRoot <directory containing include/iconv.h and lib/libiconv.a>.') }
    foreach ($file in @('libusb/libusb/core.c', 'libefex/src/efex-common.c', 'libzip/lib/zip.h', 'libxml2/CMakeLists.txt', 'qdl/src/qdl.c')) {
        if (-not (Test-Path -LiteralPath "$Root/third_party/$file")) { $problems.Add("Missing submodule file $file. Run: git submodule update --init --recursive") }
    }
    if ($problems.Count) { throw ("Environment check failed:`n - " + ($problems -join "`n - ")) }

    foreach ($entry in @(
        @('CMAKE_HOME_DIRECTORY', $Root), @('CMAKE_C_COMPILER', "$mingw/gcc.exe"),
        @('CMAKE_CXX_COMPILER', "$mingw/g++.exe"), @('CMAKE_MAKE_PROGRAM', $ninja),
        @('Qt6_DIR', "$QtRoot/lib/cmake/Qt6"), @('ZLIB_INCLUDE_DIR', $zlibInclude),
        @('ZLIB_LIBRARY_RELEASE', $zlibLibrary), @('Iconv_INCLUDE_DIR', $iconvInclude),
        @('Iconv_LIBRARY', $iconvLibrary)
    )) {
        $cached = Read-Cache $entry[0]
        # c++.exe and g++.exe from the same kit are interchangeable.
        if ($entry[0] -eq 'CMAKE_CXX_COMPILER' -and $cached -and (Split-Path $cached -Leaf) -eq 'c++.exe' -and
            (Full-Path (Split-Path $cached -Parent)) -eq $mingw) { continue }
        if ($cached -and $cached -notmatch '-NOTFOUND$' -and (Full-Path $cached) -ne (Full-Path $entry[1])) {
            $problems.Add("Cache $($entry[0])=$cached differs from $($entry[1]). Use a new -BuildDir.")
        }
    }
    $cachedGenerator = Read-Cache 'CMAKE_GENERATOR'
    if ($cachedGenerator -and $cachedGenerator -ne 'Ninja') { $problems.Add('Existing build uses another generator. Use a new -BuildDir.') }
    if ($problems.Count) { throw ($problems -join "`n") }
    Write-Output "Qt: $QtRoot`nMinGW: $mingw (GCC $gccVersion)`nCMake: $cmake`nNinja: $ninja`nGit: $git`nShell: $sh`nZlib: $ZlibRoot`nIconv: $IconvRoot`nBuild: $Build"
    if ($CheckEnvironment) { Write-Output 'Environment check passed.'; return }

    $env:PATH = (@("$QtRoot/bin", $mingw, (Split-Path $cmake -Parent), (Split-Path $ninja -Parent), (Split-Path $git -Parent), (Split-Path $sh -Parent)) + @($savedEnvironment['PATH'])) -join ';'
    foreach ($name in @('CC', 'CXX', 'CMAKE_PREFIX_PATH', 'Qt6_DIR', 'QTDIR', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($name, $null, 'Process')
    }
    # Keep a cached equivalent c++.exe path to avoid forcing a CMake cache reset.
    $cpp = Read-Cache 'CMAKE_CXX_COMPILER'
    if (-not $cpp) { $cpp = "$mingw/g++.exe" }
    Run-Checked { & $cmake -S $Root -B $Build -G Ninja "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_C_COMPILER=$mingw/gcc.exe" "-DCMAKE_CXX_COMPILER=$cpp" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DGIT_EXECUTABLE=$git" "-DZLIB_ROOT=$ZlibRoot" "-DZLIB_INCLUDE_DIR=$zlibInclude" "-DZLIB_LIBRARY_RELEASE=$zlibLibrary" "-DIconv_INCLUDE_DIR=$iconvInclude" "-DIconv_LIBRARY=$iconvLibrary" -DUPGRADE_TOOL_TESTS=ON -DCMAKE_BUILD_TYPE=Release }
    Run-Checked { & $cmake --build $Build --parallel }
    Run-Checked { & $ctest --test-dir $Build --output-on-failure }
    $Stage = Join-Path $Build "dist/UpgradeTool-GUI"
    # Rebuild staging so interrupted deployments and obsolete DLLs cannot survive.
    $stagePath = Full-Path $Stage
    $distPath = Full-Path (Join-Path $Build 'dist')
    if (-not $stagePath.StartsWith($distPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Staging path is outside the build dist directory: $stagePath"
    }
    if (Test-Path -LiteralPath $stagePath) { Remove-Item -LiteralPath $stagePath -Recurse -Force }
    New-Item -ItemType Directory -Force $Stage | Out-Null
    Copy-Item (Join-Path $Build "upgrade_tool_gui.exe") $Stage
    Copy-Item (Join-Path $Build "upgrade_driver_installer.exe") $Stage
    Copy-Item (Join-Path $Root "config.ini") $Stage
    Run-Checked { & $cmake "-DQUALCOMM_DRIVER_CACHE_DIR=$Build/qualcomm-driver-cache" "-DQUALCOMM_DRIVER_DESTINATION=$Stage/tools/qualcomm" -P (Join-Path $Root "scripts/prepare_qualcomm_driver.cmake") }
    Run-Checked { & $cmake "-DROCKUSB_DRIVER_CACHE_DIR=$Build/rockusb-driver-cache" "-DROCKUSB_DRIVER_DESTINATION=$Stage/tools/rockusb" -P (Join-Path $Root "scripts/prepare_rockusb_driver.cmake") }
    Run-Checked { & $cmake "-DALLWINNER_DRIVER_CACHE_DIR=$Build/allwinner-driver-cache" "-DALLWINNER_DRIVER_DESTINATION=$Stage/tools/allwinner" -P (Join-Path $Root "scripts/prepare_allwinner_driver.cmake") }
    $AdbDir = Join-Path $Stage "tools/adb"
    Run-Checked { & $cmake -DADB_PLATFORM=win "-DADB_CACHE_DIR=$Build/adb-cache" "-DADB_DESTINATION=$AdbDir" -P (Join-Path $Root "scripts/prepare_adb.cmake") }
    Run-Checked { & "$AdbDir/adb.exe" version }
    Run-Checked { & $cmake "-DAPP=$AdbDir/adb.exe" "-DDESTINATION=$AdbDir" "-DSEARCH_DIRS=$AdbDir;$env:PATH" -P (Join-Path $Root "scripts/collect_windows_runtime.cmake") }
    Run-Checked { & $deploy --release --compiler-runtime --qmldir (Join-Path $Root "gui/qml") (Join-Path $Stage "upgrade_tool_gui.exe") }
    $RuntimeSearch = "$Build;$env:PATH"
    Run-Checked { & $cmake "-DAPP=$(Join-Path $Stage 'upgrade_tool_gui.exe')" "-DDESTINATION=$Stage" "-DSEARCH_DIRS=$RuntimeSearch" -P (Join-Path $Root "scripts/collect_windows_runtime.cmake") }
    Run-Checked { & $cmake "-DAPP=$(Join-Path $Stage 'upgrade_driver_installer.exe')" "-DDESTINATION=$Stage" "-DSEARCH_DIRS=$RuntimeSearch" -P (Join-Path $Root "scripts/collect_windows_runtime.cmake") }
    $Licenses = Join-Path $Stage "licenses"
    New-Item -ItemType Directory -Force $Licenses | Out-Null
    Copy-Item (Join-Path $Root "backends/rockchip/legacy/license.txt") (Join-Path $Licenses "upgrade-tool-GPL-2.0.txt")
    Copy-Item (Join-Path $Root "third_party/libusb/COPYING") (Join-Path $Licenses "libusb-LGPL-2.1.txt")
    Copy-Item (Join-Path $Root "third_party/libefex/LICENSE") (Join-Path $Licenses "libefex-MIT.txt")
    Copy-Item (Join-Path $Root "backends/allwinner/LICENSE.OpenixCLI") (Join-Path $Licenses "OpenixCLI-MIT.txt")
    Copy-Item (Join-Path $Root "third_party/qdl/LICENSE") (Join-Path $Licenses "qdl-BSD-3-Clause.txt")
    Copy-Item (Join-Path $Root "third_party/libzip/LICENSE") (Join-Path $Licenses "libzip-BSD-3-Clause.txt")
    Copy-Item (Join-Path $Root "third_party/libxml2/Copyright") (Join-Path $Licenses "libxml2-MIT.txt")
    $versionHeader = Get-Content -LiteralPath (Join-Path $Root 'application/Version.h') -Raw
    if ($versionHeader -notmatch 'TOOL_VERSION\s*=\s*"(\d+\.\d+\.\d+)"') {
        throw 'Could not parse TOOL_VERSION from application/Version.h'
    }
    $portableName = "UpgradeTool-GUI-v$($Matches[1])-windows-x64-portable.exe"
    & (Join-Path $PSScriptRoot 'windows/package_portable.ps1') -Stage $Stage -Output (Join-Path $Build "dist/$portableName") -CompilerBin $mingw
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
}
