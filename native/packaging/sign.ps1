<#
  Signs the DLL/EXE (and the .msix if present) with the self-signed cert
  created by create-cert.ps1. Optional — only needed for a signed package.
#>
param(
    [string]$InstallDir = (Join-Path $PSScriptRoot "..\build\x64\Release"),
    [string]$PfxPath = (Join-Path $PSScriptRoot "certs\LegacyMenuBridge.pfx"),
    [string]$Password = "LegacyMenuBridge"
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path $PfxPath)) { throw "PFX not found. Run create-cert.ps1 first." }

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) { throw "signtool.exe not found - install the Windows SDK." }

& $signtool sign /fd SHA256 /f $PfxPath /p $Password (Join-Path $InstallDir "LegacyMenuBridge.dll")
& $signtool sign /fd SHA256 /f $PfxPath /p $Password (Join-Path $InstallDir "LegacyMenuBridge.exe")
if (Test-Path (Join-Path $PSScriptRoot "LegacyMenuBridge.msix")) {
    & $signtool sign /fd SHA256 /f $PfxPath /p $Password (Join-Path $PSScriptRoot "LegacyMenuBridge.msix")
}
Write-Host "Signed." -ForegroundColor Green
