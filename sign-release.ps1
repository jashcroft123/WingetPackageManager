param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{40}$')][string]$Thumbprint,
    [string]$ExePath=(Join-Path $PSScriptRoot 'dist\WingetSwift.exe'),
    [string]$TimestampUrl='https://timestamp.digicert.com'
)
$ErrorActionPreference='Stop'
$certificate=Get-Item -LiteralPath "Cert:\CurrentUser\My\$Thumbprint"
if(!$certificate.HasPrivateKey){throw 'The certificate has no accessible private key.'}
if($certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date)){throw 'The certificate is not currently valid.'}
if(!($certificate.EnhancedKeyUsageList | Where-Object {$_.ObjectId -eq '1.3.6.1.5.5.7.3.3'})){throw 'A code-signing certificate is required.'}
$chain=[Security.Cryptography.X509Certificates.X509Chain]::new()
try {if(!$chain.Build($certificate)){throw 'The certificate chain is not trusted. Use a publicly trusted code-signing certificate.'}} finally {$chain.Dispose()}
$signTool=Get-Command signtool.exe -ErrorAction SilentlyContinue
if($signTool){$signToolPath=$signTool.Source}else{
    $sdkBin=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $signToolPath=Get-ChildItem -LiteralPath $sdkBin -Directory | Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'x64\signtool.exe'} | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
}
if(!$signToolPath){throw 'Install the Windows SDK signing tools.'}
& $signToolPath sign /sha1 $Thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $ExePath
if($LASTEXITCODE){throw 'Signing failed.'}
& $signToolPath verify /pa /all $ExePath
if($LASTEXITCODE){throw 'Signature verification failed.'}
$signature=Get-AuthenticodeSignature -LiteralPath $ExePath
if($signature.Status -ne 'Valid'){throw "Signature status: $($signature.Status)"}
Get-FileHash -LiteralPath $ExePath -Algorithm SHA256
Write-Output 'Signed and verified. Repackage the portable ZIP from this signed EXE before publishing. New signed builds can still show SmartScreen reputation warnings.'
