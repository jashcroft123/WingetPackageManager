param([switch]$Diagnostics,[ValidateSet('amd64','x86')][string]$Architecture='x86',[switch]$Analyze)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
 if (!$vs) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
 $env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + $env:PATH
 & (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch $Architecture -HostArch amd64 -SkipAutomaticLocation
 $output=if($Diagnostics){'dist\native-c-diagnostics'}else{'dist\native-c'}; if($Architecture -eq 'amd64'){$output+='-x64'}
 New-Item -ItemType Directory -Force $output | Out-Null
 $name=if($Architecture -eq 'amd64'){'WingetSwiftGuiC-x64.exe'}else{'WingetSwiftGuiC.exe'}
 [string[]]$sources=@('src\native_c.c','src\tui_core.c');[string[]]$defines=@();if($Diagnostics){$defines+='/DWINGETSWIFT_DIAGNOSTICS';$sources+='src\tui-core-tests.c'};if($Analyze){$defines+='/analyze'}
 & cl.exe /nologo /utf-8 /std:c17 /O1 /Oi /GL /MT /GS /Gw /Gy /W4 /DUNICODE /D_UNICODE @sources @defines "/Fe:$output\$name" "/Fo:$output\" /link /LTCG /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT /NODEFAULTLIB:libucrt.lib ucrt.lib /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest comctl32.lib comdlg32.lib shell32.lib user32.lib gdi32.lib advapi32.lib wintrust.lib crypt32.lib
 if($LASTEXITCODE){throw 'Native C build failed'}
 if(!$Diagnostics){Copy-Item -LiteralPath "$output\$name" -Destination $name -Force}
 Write-Output "Native C EXE: $((Get-Item "$output\$name").Length) bytes"
} finally { Pop-Location }
