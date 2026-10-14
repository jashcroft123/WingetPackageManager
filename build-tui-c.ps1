param([switch]$Diagnostics,[ValidateSet('amd64','x86')][string]$Architecture='x86')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 $vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 if(!$vs){throw 'Install Visual Studio Build Tools with Desktop development with C++.'}
 $env:PATH="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;"+$env:PATH
 & (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
 $output=if($Diagnostics){'dist\tui-c-diagnostics'}else{'dist\tui-c'}
 if($Architecture -eq 'amd64'){$output+='-x64'}
 New-Item -ItemType Directory -Force $output | Out-Null
 [string[]]$defines=@();[string[]]$sources=@('src\tui.c','src\tui_core.c')
 if($Diagnostics){$defines+='/DWINGETSWIFT_DIAGNOSTICS';$sources+='src\tui-core-tests.c'}
 & cl.exe /nologo /utf-8 /std:c17 /O1 /Oi /GL /MT /GS /Gw /Gy /W4 /DUNICODE /D_UNICODE @defines @sources "/Fe:$output\WingetSwiftTuiC.exe" "/Fo:$output\\" /link /LTCG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB:libucrt.lib ucrt.lib /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:tui.manifest shell32.lib wintrust.lib crypt32.lib
 if($LASTEXITCODE){throw 'C TUI build failed'}
 if(!$Diagnostics){
  $rootExe=if($Architecture -eq 'amd64'){'WingetSwiftTuiC-x64.exe'}else{'WingetSwiftTuiC.exe'}
  Copy-Item -LiteralPath "$output\WingetSwiftTuiC.exe" -Destination $rootExe
 }
 Write-Output "C TUI EXE: $((Get-Item "$output\WingetSwiftTuiC.exe").Length) bytes"
} finally {Pop-Location}
