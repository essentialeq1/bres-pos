<#
  Registers the sparse package (idempotent). Exits 0 on success, 1 on failure.
#>
param(
    [string]$InstallDir = "",
    [string]$Manifest   = (Join-Path $PSScriptRoot "AppxManifest.xml")
)

if (-not $InstallDir) {
    if (Test-Path (Join-Path $PSScriptRoot "LegacyMenuBridge.dll")) { $InstallDir = $PSScriptRoot }
    else { $InstallDir = Join-Path $PSScriptRoot "..\build\x64\Release" }
}
$InstallDir = (Resolve-Path $InstallDir).Path
if (-not (Test-Path (Join-Path $InstallDir "LegacyMenuBridge.dll"))) {
    Write-Host "ERROR: LegacyMenuBridge.dll not found in $InstallDir" -ForegroundColor Red
    exit 1
}

Get-Process -Name LegacyMenuBridge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$destManifest = Join-Path $InstallDir "AppxManifest.xml"
if ([IO.Path]::GetFullPath($Manifest) -ne [IO.Path]::GetFullPath($destManifest)) {
    Copy-Item $Manifest $destManifest -Force
}
$assetsSrc = Join-Path $PSScriptRoot "Assets"
$assetsDst = Join-Path $InstallDir "Assets"
if ((Test-Path $assetsSrc) -and ([IO.Path]::GetFullPath($assetsSrc) -ne [IO.Path]::GetFullPath($assetsDst))) {
    Copy-Item $assetsSrc $assetsDst -Recurse -Force
}
$blocklistSrc = Join-Path $PSScriptRoot "LegacyMenuBridge.blocklist.txt"
$blocklistDst = Join-Path $InstallDir "LegacyMenuBridge.blocklist.txt"
if ((Test-Path $blocklistSrc) -and ([IO.Path]::GetFullPath($blocklistSrc) -ne [IO.Path]::GetFullPath($blocklistDst))) {
    Copy-Item $blocklistSrc $blocklistDst -Force
}

try {
    $existing = Get-AppxPackage -Name LegacyMenuBridge -ErrorAction SilentlyContinue
    if ($existing) {
        Write-Host "Removing previous registration ($($existing.Version))..." -ForegroundColor Yellow
        $existing | Remove-AppxPackage -ErrorAction Stop
    }
} catch {
    Write-Host "WARNING: could not remove previous registration: $($_.Exception.Message)" -ForegroundColor Yellow
}

try {
    Write-Host "Registering sparse package from $InstallDir ..." -ForegroundColor Cyan
    Add-AppxPackage -Register $destManifest -ExternalLocation $InstallDir -ErrorAction Stop
    Write-Host "Registered." -ForegroundColor Green
    exit 0
} catch {
    Write-Host ("REGISTRATION FAILED: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
