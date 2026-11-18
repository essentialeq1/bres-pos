<#
  Activation self-check, driven by CLSID.
  The DLL also logs "dll attached" to %TEMP%\LegacyMenuBridgeNative-*.log the
  first time Explorer activates it, which is the definitive confirmation.
#>
$clsid = [guid]"B7F3A2C1-9D4E-4F6A-8B2C-5E1D3A7F9B04"
try {
    $t = [Type]::GetTypeFromCLSID($clsid)
    $o = [Activator]::CreateInstance($t)
    Write-Host "Activation OK" -ForegroundColor Green
    try { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($o) } catch {}
} catch {
    Write-Host "Activation check inconclusive: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "Right-click once, then check %TEMP%\LegacyMenuBridgeNative-*.log for 'dll attached'."
}
