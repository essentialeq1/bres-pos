<#
  Tries to activate the COM class and prints the exact HRESULT on failure.
  Run this after install.bat if "Classic Menu" does not appear.
#>
$ErrorActionPreference = "Continue"
try {
    $obj = New-Object -ComObject "LegacyMenuBridge.Command"
    Write-Host "Activation OK" -ForegroundColor Green
    try { [void][System.Runtime.InteropServices.Marshal]::FinalReleaseComObject($obj) } catch {}
} catch {
    $msg = $_.Exception.Message
    if ($_.Exception.InnerException -and $_.Exception.InnerException.Message) {
        $msg += " | Inner: " + $_.Exception.InnerException.Message
    }
    Write-Host "Activation FAILED: $msg" -ForegroundColor Red
}
