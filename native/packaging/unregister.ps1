<#
  Removes the LegacyMenuBridge sparse package registration and stops the worker.
#>
$ErrorActionPreference = "Stop"
Get-Process -Name LegacyMenuBridge -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$pkg = Get-AppxPackage -Name LegacyMenuBridge -ErrorAction SilentlyContinue
if (-not $pkg) { Write-Host "Not registered."; return }
Write-Host "Removing LegacyMenuBridge..."
$pkg | Remove-AppxPackage
Write-Host "Unregistered LegacyMenuBridge." -ForegroundColor Green
