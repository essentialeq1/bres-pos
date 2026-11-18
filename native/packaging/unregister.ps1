<#
  Removes the LegacyMenuBridge sparse package registration.
#>
$ErrorActionPreference = "Stop"
$pkg = Get-AppxPackage -Name "LegacyMenuBridge"
if (-not $pkg) { Write-Host "Not registered."; return }
$pkg | Remove-AppxPackage
Write-Host "Unregistered LegacyMenuBridge." -ForegroundColor Green
