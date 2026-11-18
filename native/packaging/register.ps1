<#
  Registers the sparse package so "Classic Menu" appears in the Windows 11
  modern context menu.

  Requires a prior build (build.ps1) and either Developer Mode enabled or a
  trusted self-signed certificate (create-cert.ps1).
#>
param(
    [string]$InstallDir = (Join-Path $PSScriptRoot "..\build\x64\Release"),
    [string]$Manifest   = (Join-Path $PSScriptRoot "AppxManifest.xml")
)

$ErrorActionPreference = "Stop"
$InstallDir = (Resolve-Path $InstallDir).Path

if (-not (Test-Path (Join-Path $InstallDir "LegacyMenuBridge.dll"))) {
    throw "LegacyMenuBridge.dll not found in $InstallDir. Run build.ps1 first."
}

# Everything the manifest references (dll/exe/assets) must sit next to it.
Copy-Item $Manifest (Join-Path $InstallDir "AppxManifest.xml") -Force
Copy-Item (Join-Path $PSScriptRoot "Assets") (Join-Path $InstallDir "Assets") -Recurse -Force

Write-Host "Registering sparse package..." -ForegroundColor Cyan
Add-AppxPackage -Register (Join-Path $InstallDir "AppxManifest.xml") -ExternalLocation $InstallDir

Write-Host "Registered." -ForegroundColor Green
Write-Host "If the entry does not appear, restart Explorer (or sign out and in)."
Write-Host "Right-click a file or folder -> 'Classic Menu'."
