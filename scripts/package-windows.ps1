param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "",
    [string]$DistDir = "",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"

if (-not $BuildDir) { $BuildDir = Join-Path $ProjectRoot "build" }
if (-not $DistDir) { $DistDir = Join-Path $ProjectRoot "dist\windows" }
if (-not $Version) {
    $match = Select-String -Path (Join-Path $ProjectRoot "CMakeLists.txt") -Pattern 'project\(Rawatib VERSION ([0-9.]+)'
    $Version = $match.Matches[0].Groups[1].Value
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$stageDir = Join-Path $DistDir "Rawatib"
if (Test-Path $stageDir) { Remove-Item -Recurse -Force $stageDir }
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

Copy-Item -Path (Join-Path $BuildDir "Rawatib.exe") -Destination $stageDir

$windeployqt = Get-Command windeployqt.exe -ErrorAction Stop
& $windeployqt.Source --release --compiler-runtime --no-translations (Join-Path $stageDir "Rawatib.exe")

$mingwBin = Split-Path (Get-Command g++.exe -ErrorAction Stop).Source
$extraDlls = @(
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll",
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll"
)

foreach ($dll in $extraDlls) {
    $source = Join-Path $mingwBin $dll
    if (Test-Path $source) {
        Copy-Item $source -Destination $stageDir -Force
    }
}

$portableZip = Join-Path $DistDir ("Rawatib-{0}-windows-portable.zip" -f $Version)
if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $portableZip

$makensis = Get-Command makensis.exe -ErrorAction Stop
& $makensis.Source `
    /DAPP_VERSION=$Version `
    /DAPP_PUBLISHER=FakyLab `
    /DAPP_CONTACT=fakylab@proton.me `
    /DSOURCE_DIR=$stageDir `
    /DOUTPUT_FILE=$(Join-Path $DistDir ("Rawatib-{0}-Setup.exe" -f $Version)) `
    (Join-Path $ProjectRoot "packaging\windows\rawatib_installer.nsi")
