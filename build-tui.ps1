param([switch]$Diagnostics,[ValidateSet('amd64','x86')][string]$Architecture='x86')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 $vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 if(!$vs){throw 'Install Visual Studio Build Tools with Desktop development with C++.'}
 $env:PATH="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;"+$env:PATH
 $vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
 $output=if($Diagnostics){'dist\tui-diagnostics'}else{'dist\tui'}
 if($Architecture -eq 'amd64'){$output+='-x64'}
 New-Item -ItemType Directory -Force $output | Out-Null
 $define=if($Diagnostics){'/DWINGETSWIFT_DIAGNOSTICS'}else{''}
 $target=if($Architecture -eq 'amd64'){'x64'}else{'x86'}
 $command='"'+$vcvars+'" '+$target+' && cl /nologo /utf-8 /std:c++17 /O1 /Oi /GL /MT /EHsc /GR- /Gw /Gy /W4 /GS /DUNICODE /D_UNICODE '+$define+' src\tui.cpp /Fe:'+ $output+'\WingetSwift-TUI.exe /Fo:'+$output+'\tui.obj /link /LTCG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB:libucrt.lib ucrt.lib /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:tui.manifest shell32.lib user32.lib wintrust.lib crypt32.lib'
 & cmd.exe /d /s /c $command
 if($LASTEXITCODE){throw 'TUI build failed'}
 Write-Output "TUI EXE: $((Get-Item "$output\WingetSwift-TUI.exe").Length) bytes"
} finally {Pop-Location}
