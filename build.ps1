param([switch]$Diagnostics,[ValidateSet('amd64','x86')][string]$Architecture='x86')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & (Join-Path $PSScriptRoot 'build-native.ps1') -Diagnostics:$Diagnostics -Architecture $Architecture
    if (!$Diagnostics) {
        $releaseDir=if($Architecture -eq 'amd64'){'dist\native-x64'}else{'dist\native'}
        Copy-Item -LiteralPath (Join-Path $releaseDir 'WingetSwift.exe') -Destination dist\WingetSwift.exe
        $rootExe=if($Architecture -eq 'amd64'){'WingetSwift-x64.exe'}else{'WingetSwift.exe'}
        Copy-Item -LiteralPath (Join-Path $releaseDir 'WingetSwift.exe') -Destination $rootExe
    }
} finally {
    Pop-Location
}
