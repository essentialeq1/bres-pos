<#
  Builds the native LegacyMenuBridge DLL + host EXE with CMake/MSVC.
  Run from a "Developer PowerShell for VS 2022" (cmake + MSVC on PATH).
#>
param(
    [string]$Configuration = "Release",
    [string]$BuildRoot = (Join-Path $PSScriptRoot "..\build\x64")
)

$ErrorActionPreference = "Stop"
$source = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

Write-Host "[1/2] Configuring (x64)..." -ForegroundColor Cyan
cmake -S $source -B $BuildRoot -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "[2/2] Building ($Configuration)..." -ForegroundColor Cyan
cmake --build $BuildRoot --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$outDir = Join-Path $BuildRoot $Configuration
Write-Host "Done. Binaries in: $outDir" -ForegroundColor Green
Get-ChildItem $outDir | Select-Object Name, Length | Format-Table
