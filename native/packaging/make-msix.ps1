<#
  Builds a sparse .msix (manifest + assets only; binaries stay external).
  Install with:  Add-AppxPackage -Path .\LegacyMenuBridge.msix -ExternalLocation <dir>
#>
param(
    [string]$OutPath = (Join-Path $PSScriptRoot "LegacyMenuBridge.msix")
)
$ErrorActionPreference = "Stop"

$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $makeappx) { throw "makeappx.exe not found - install the Windows SDK." }

$stage = Join-Path $env:TEMP ("LegacyMenuBridge-msix-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $PSScriptRoot "AppxManifest.xml") $stage -Force
Copy-Item (Join-Path $PSScriptRoot "Assets") $stage -Recurse -Force

& $makeappx pack /d $stage /p $OutPath /o
if ($LASTEXITCODE -ne 0) { throw "makeappx failed" }
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "Created $OutPath (sign it with sign.ps1 before Add-AppxPackage)" -ForegroundColor Green
