param([switch]$Diagnostics,[ValidateSet('amd64','x86')][string]$Architecture='x86')
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + $env:PATH
& (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
$outputDir=if($Diagnostics){'dist\diagnostics'}else{'dist\compact'}
if($Architecture -eq 'amd64'){$outputDir += '-x64'}
New-Item -ItemType Directory -Force $outputDir | Out-Null
[string[]]$defines=@();if($Diagnostics){$defines+= '/DWINGETSWIFT_DIAGNOSTICS'}
& cl.exe /nologo /utf-8 /std:c++17 /O1 /GL /MT /EHsc /GR- /Gw /Gy /W4 /DUNICODE /D_UNICODE src\compact.cpp @defines "/Fe:$outputDir\WingetSwift.exe" "/Fo:$outputDir\main.obj" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest comctl32.lib comdlg32.lib shell32.lib user32.lib gdi32.lib advapi32.lib dwmapi.lib uxtheme.lib
if ($LASTEXITCODE) { throw 'Compact build failed' }
$bytes = (Get-Item "$outputDir\WingetSwift.exe").Length

Write-Output "Standalone EXE: $bytes bytes"
