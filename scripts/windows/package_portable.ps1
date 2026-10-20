<# Build the single-file launcher from an already deployed application directory. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Stage,
    [Parameter(Mandatory=$true)][string]$Output,
    [Parameter(Mandatory=$true)][string]$CompilerBin
)
$ErrorActionPreference = 'Stop'
$Stage = (Resolve-Path -LiteralPath $Stage).Path
$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Invoke-Native([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
# Keep generated inputs inside staging, but exclude them from the application payload.
$work = Join-Path $Stage '.single-file'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$files = @(Get-ChildItem -LiteralPath $Stage -File -Recurse |
    Where-Object { -not $_.FullName.StartsWith($work + '\', [StringComparison]::OrdinalIgnoreCase) } |
    Sort-Object FullName)
if (-not (Test-Path -LiteralPath "$Stage/upgrade_tool_gui.exe")) { throw 'Missing deployed GUI executable.' }
$manifest = [Collections.Generic.List[string]]::new()
$ddf = [Collections.Generic.List[string]]::new()
@('.OPTION EXPLICIT', '.Set Cabinet=ON', '.Set Compress=ON', '.Set CompressionType=LZX',
  '.Set CompressionMemory=21', '.Set MaxDiskSize=0', '.Set MaxCabinetSize=0',
  '.Set CabinetNameTemplate=payload.cab',
  '.Set InfFileName=.single-file/setup.inf', '.Set RptFileName=.single-file/setup.rpt',
  '.Set DiskDirectoryTemplate=.single-file') | ForEach-Object { $ddf.Add($_) }
for ($index = 0; $index -lt $files.Count; ++$index) {
    $file = $files[$index]
    $relative = $file.FullName.Substring($Stage.Length + 1)
    if ($relative -match '[^\x20-\x7e]' -or $relative -match '["\r\n\t]') {
        throw "Cabinet source names must be printable ASCII: $relative"
    }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest.Add("$hash`t$($file.Length)`t$($relative.Replace('\', '/'))")
    $ddf.Add(('"{0}" "{1}"' -f $relative, $index))
}
[IO.File]::WriteAllText("$work/manifest.tsv", ($manifest -join "`n") + "`n", $utf8)
[IO.File]::WriteAllText("$work/payload.ddf", ($ddf -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
Push-Location -LiteralPath $Stage
try {
    Write-Output "Compressing $($files.Count) runtime files..."
    & "$env:SystemRoot/System32/makecab.exe" /F '.single-file/payload.ddf' > '.single-file/makecab.log'
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath '.single-file/makecab.log' -Tail 20
        throw "makecab.exe failed with exit code $LASTEXITCODE"
    }
}
finally { Pop-Location }
$cab = Join-Path $work 'payload.cab'
if (-not (Test-Path -LiteralPath $cab) -or (Get-Item -LiteralPath $cab).Length -ge 0x7fffffff) {
    throw 'Single-file Cabinet payload is missing or exceeds the supported 2 GiB limit.'
}
$identity = (Get-FileHash -LiteralPath $cab -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$work/identity.txt", $identity, $utf8)
$resource = @('101 RCDATA "payload.cab"', '102 RCDATA "manifest.tsv"', '103 RCDATA "identity.txt"') -join "`n"
$resource += "`n" + [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'app_icon.rc'))
[IO.File]::WriteAllText("$work/payload.rc", $resource, [Text.Encoding]::ASCII)
Push-Location -LiteralPath $work
try {
    Invoke-Native "$CompilerBin/windres.exe" @('-I', $PSScriptRoot, '-i', 'payload.rc', '-o', 'payload.o')
    $temporaryOutput = Join-Path $work 'UpgradeTool-GUI.exe'
    Invoke-Native "$CompilerBin/g++.exe" @('-std=c++20', '-O2', '-Wall', '-Wextra', '-static',
        '-municode', '-mwindows', (Join-Path $PSScriptRoot 'portable_launcher.cpp'), 'payload.o',
        '-o', $temporaryOutput, '-lcabinet', '-lbcrypt', '-lshell32', '-lole32', '-luuid')
    Copy-Item -LiteralPath $temporaryOutput -Destination $Output -Force
} finally { Pop-Location }
Write-Output "Single-file application: $Output"
Write-Output "Runtime cache: %LOCALAPPDATA%\upgrade_tool\runtime\$identity"
