<#
  Registers the sparse package so "Classic Menu" appears in the Windows 11
  modern context menu.

  Works both:
    * from the unzipped release folder (dll/exe/manifest/assets beside this script), and
    * from native\packaging after a local build (binaries in ..\build\x64\Release).

  Requires Developer Mode enabled (Settings > Privacy & Security > For developers).
#>
param(
    [string]$InstallDir = "",
    [string]$Manifest   = (Join-Path $PSScriptRoot "AppxManifest.xml")
)

$ErrorActionPreference = "Stop"

if (-not $InstallDir) {
    if (Test-Path (Join-Path $PSScriptRoot "LegacyMenuBridge.dll")) {
        $InstallDir = $PSScriptRoot
    } else {
        $InstallDir = Join-Path $PSScriptRoot "..\build\x64\Release"
    }
}

$InstallDir = (Resolve-Path $InstallDir).Path
if (-not (Test-Path (Join-Path $InstallDir "LegacyMenuBridge.dll"))) {
    throw "LegacyMenuBridge.dll not found in $InstallDir."
}

$destManifest = Join-Path $InstallDir "AppxManifest.xml"
if ([IO.Path]::GetFullPath($Manifest) -ne [IO.Path]::GetFullPath($destManifest)) {
    Copy-Item $Manifest $destManifest -Force
}

$assetsSrc = Join-Path $PSScriptRoot "Assets"
$assetsDst = Join-Path $InstallDir "Assets"
if (Test-Path $assetsSrc) {
    if ([IO.Path]::GetFullPath($assetsSrc) -ne [IO.Path]::GetFullPath($assetsDst)) {
        Copy-Item $assetsSrc $assetsDst -Recurse -Force
    }
}

Write-Host "Registering sparse package from $InstallDir ..." -ForegroundColor Cyan
Add-AppxPackage -Register $destManifest -ExternalLocation $InstallDir

Write-Host "Registered." -ForegroundColor Green
Write-Host "If 'Classic Menu' does not appear, restart Explorer (Task Manager > Windows Explorer > Restart)."
