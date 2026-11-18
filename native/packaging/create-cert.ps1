<#
  Creates a self-signed code-signing cert for "CN=LegacyMenuBridge", exports it,
  and trusts it for the current user. The Publisher in AppxManifest.xml must
  match this Subject exactly.
#>
param(
    [string]$Subject = "CN=LegacyMenuBridge",
    [string]$OutDir = (Join-Path $PSScriptRoot "certs"),
    [string]$PfxPassword = "LegacyMenuBridge"
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -KeyUsage DigitalSignature `
    -FriendlyName "LegacyMenuBridge dev signing" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

$cerPath = Join-Path $OutDir "LegacyMenuBridge.cer"
$pfxPath = Join-Path $OutDir "LegacyMenuBridge.pfx"
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

$pwd = ConvertTo-SecureString -String $PfxPassword -AsPlainText -Force
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $pwd | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation "Cert:\CurrentUser\TrustedPeople" | Out-Null

Write-Host "Thumbprint: $($cert.Thumbprint)" -ForegroundColor Green
Write-Host "Trusted in Cert:\CurrentUser\TrustedPeople"
Write-Host "PFX: $pfxPath"
