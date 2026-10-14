param([switch]$Diagnostics,[ValidateSet('amd64','x86')][string]$Architecture='x86')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 if (!$vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
 $env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + $env:PATH
 & (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
 $output=if($Diagnostics){'dist\native-diagnostics'}else{'dist\native'}
 if($Architecture -eq 'amd64'){$output+='-x64'}
 New-Item -ItemType Directory -Force $output | Out-Null
 [string[]]$defines=@();if($Diagnostics){$defines+='/DWINGETSWIFT_DIAGNOSTICS'}
 # Static C++/vcruntime with the Windows 10/11 system UCRT (Microsoft Hybrid CRT).
 & cl.exe /nologo /utf-8 /std:c++17 /O1 /Oi /GL /MT /EHsc /GR- /Gw /Gy /W4 /DUNICODE /D_UNICODE src\native.cpp @defines "/Fe:$output\WingetSwift.exe" "/Fo:$output\main.obj" /link /LTCG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB:libucrt.lib ucrt.lib /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest comctl32.lib comdlg32.lib shell32.lib user32.lib gdi32.lib
 if($LASTEXITCODE){throw 'Native build failed'}
 Write-Output "Native EXE: $((Get-Item "$output\WingetSwift.exe").Length) bytes"
} finally { Pop-Location }
