param([string[]]$Targets = @('all'))
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ Build Tools are required.' }
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;" + $env:PATH
& (Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cmakeRoot = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$env:PATH = (Join-Path $cmakeRoot 'CMake\bin') + ';' + (Join-Path $cmakeRoot 'Ninja') + ';' + $env:PATH
$env:CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS = '-C target-feature=+crt-static'
& cmake -S . -B .build\modern -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Slint configuration failed' }
& cmake --build .build\modern --parallel 4 --target $Targets
if ($LASTEXITCODE) { throw 'Slint build failed' }
