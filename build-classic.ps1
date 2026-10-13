$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$dev = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + $env:PATH
& $dev -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
New-Item -ItemType Directory -Force dist | Out-Null
& cl.exe /nologo /utf-8 /std:c++17 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE src\main.cpp /Fe:dist\WingetSwiftClassic.exe /Fo:dist\classic.obj /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest comctl32.lib comdlg32.lib shell32.lib user32.lib gdi32.lib advapi32.lib dwmapi.lib uxtheme.lib
if ($LASTEXITCODE) { throw 'Build failed' }
