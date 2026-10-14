# Winget Swift

A native C++ Windows GUI for WinGet. Single portable EXE, no .NET, Electron, WebView, or third-party runtime required. Windows 10/11 with Microsoft App Installer (`winget`) is required. The default is x86 for minimum size; an x64 build is also available.

Run `dist/WingetSwift.exe`.

[Download the portable EXE](dist/WingetSwift.exe?raw=true) or [portable ZIP](dist/WingetSwift-portable.zip?raw=true).

The recommended build is the modern **compact native UI**, at **182,784 bytes (183 KB / 179 KiB)**. It recreates the sidebar, rounded controls, spacious rows and collapsible activity panel without Slint. Package-list and activity-log scrollbars use slim rounded thumbs matching the light/dark panel colors, with Windows handling scrolling and high-contrast appearance. Controls adapt to narrow windows and DPI changes to prevent overlapping buttons. See **COMPACT.md** for measurements. The earlier classic build is `dist/WingetSwiftClassic.exe`; the larger Slint trial remains at `dist/modern/WingetSwiftModern.exe`.

The interface follows Windows' default **app** light/dark mode and updates when Windows theme settings change. Window backgrounds, text fields, package lists, column headers, buttons, and the title bar adapt. Windows high-contrast mode takes priority. Native dialogs and scrollbars retain the appearance supplied by Windows.

- **Discover**: type a name or ID, press Enter or Search, select a result, then Install selected. Searches use the community `winget` source and return up to 1,000 results; narrow the query when needed.
- **Installed**: filter immediately by name or ID, select a package, then Remove selected.
- **Updates**: view available upgrades and update selected packages.
- **Refresh**: bypass the cache and request fresh results.
- **Export setup**: saves a native WinGet JSON export, including installed versions.
- **Import setup**: choose that JSON on another machine to install the latest available versions. Review the file before importing. The EXE and WinGet must be available on that machine.

Export covers packages WinGet can identify in its sources. It does not copy application data, settings, licenses, or installers. WinGet reports skipped/unavailable packages in the output panel. The equivalent migration commands are documented by Microsoft: [export](https://learn.microsoft.com/en-us/windows/package-manager/winget/export) and [import](https://learn.microsoft.com/en-us/windows/package-manager/winget/import).

Package changes require a confirmation dialog. WinGet receives exact package IDs and source identifiers, never a shell command. Package/source agreements are accepted after confirmation. Installers may display UI or request elevation. The app itself runs without elevation.

Use **Ctrl-click** to select separate packages or **Shift-click** to select a range in any package list. Install, update, and remove apply to every selected package after one confirmation. Packages run sequentially; failures are logged and remaining packages continue. A timeout stops the remaining batch. The output includes per-package results and a success/failure summary. Filtering or refreshing clears selections to prevent stale row targets.

## Performance and cache

The UI opens before WinGet runs. A virtual list avoids creating an individual UI object for every package. All WinGet work happens on a background thread. The static C++ runtime is linked into the EXE.

Disk caches are reused across launches: installed packages for 10 minutes, updates for 5 minutes, and remote queries for 24 hours. Expired results display immediately while refreshing. A maximum of 128 cache files is retained. Package changes invalidate the caches; failed changes clear the visible list and leave the error available. Refresh explicitly bypasses caching. Cached results can be viewed without network access; fresh remote searches require network access.

By default cache files live under `%LOCALAPPDATA%\WingetSwift\cache`. Run `WingetSwift.exe --portable` to keep them beside the EXE instead (the folder must be writable). Delete the cache folder to reset it.

The recommended release is 179 KiB. Before the repaint fixes, release launch-to-responsive-window medians were 68 ms for x86 and 61 ms for x64 across ten warm runs. The historical matched fixture-render benchmark measured 79 ms for compact, 87 ms for classic, and 157 ms for Slint. These use different readiness endpoints and should not be directly compared. WinGet's network/source startup remains outside the app's control. Details and raw results are in COMPACT.md.

## Build

Install Visual Studio Build Tools with **Desktop development with C++** and a Windows SDK. From this directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Output: `dist/WingetSwift.exe` and `dist/compact/WingetSwift.exe`. The EXE is unsigned. The compact build uses static CRT linkage plus size/link-time optimization. Test and benchmark code stays out of the shipped EXE. Use `build-compact.ps1 -Architecture amd64` for `dist/compact-x64/WingetSwift.exe`, or `build-classic.ps1` for the earlier UI.

To build the modern version, also install Rust 1.92+ and the Visual Studio CMake/Ninja tools, then run `powershell -NoProfile -ExecutionPolicy Bypass -File .\build-modern.ps1`. It downloads pinned Slint 1.18.0 and builds its static runtime on the first run, which takes several minutes. Output: `dist/modern/WingetSwiftModern.exe`. UI assets are embedded; no Slint or Visual C++ runtime DLL needs to be installed. Slint attribution is available through the About button; its license and third-party notices accompany the modern archive.

Closing either app during a read-only refresh cancels that WinGet process. Closing during package changes is blocked until completion. Lists refresh after partial batch failures, with per-package results retained in the activity output.

## Verification

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Diagnostics
Start-Process .\dist\diagnostics\WingetSwift.exe --self-test -Wait
Start-Process .\dist\diagnostics\WingetSwift.exe --ui-test -Wait
Start-Process .\dist\diagnostics\WingetSwift.exe --live-test -Wait
Start-Process .\dist\diagnostics\WingetSwift.exe --benchmark -Wait
```

Results go to `test-results.txt`, `ui-test.txt`, `live-test.txt`, and `startup-ms.txt` in the working directory. Live tests read remote/installed/updatable packages, check an empty search, and create then remove a temporary WinGet export. They do not install, update, import, or remove applications. Exit code 0 means success. Benchmark initializes the window and exits without running WinGet.

UI tests also render `theme-light.bmp` and `theme-dark.bmp`, verify both palettes and the Windows settings-change notification, and check that repeated theme changes release GDI resources. They do not modify Windows theme settings.

The CLI currently emits human-readable tables rather than a structured list format. The parser handles UTF-8, carriage-return progress, ANSI color escapes, CJK column widths, and repeated tables. This release is validated against English WinGet 1.29.380 output; localized headers and unusual Unicode display widths may require adaptations. Truncated package IDs are blocked from package actions. Read operations time out after two minutes; each package operation after one hour. An installer already started by WinGet may continue after a timeout; inspect the output and installer before retrying. There is no cancellation feature in this release.

Background WinGet checks and package operations keep navigation, filtering, and selection available. New checks queue the latest request and cancel obsolete read-only checks; results from another view never replace the current list. Package changes still run one at a time, with conflicting operation buttons disabled until completion.

Update feedback shows elapsed time, animated activity, and the current package's position in a batch. Animation stops on completion. Rounded controls and scrollbar thumbs use Windows GDI+ antialiasing. Win32 cache I/O and manual line parsing reduce embedded runtime code while preserving existing cache files.

## Signing and SmartScreen

The checked-in EXE is unsigned. SmartScreen may warn because an unsigned/new binary has insufficient reputation; a trusted signature helps establish publisher identity, but does not guarantee immediate acceptance. Self-signing does not establish public SmartScreen reputation. See [Microsoft's explanation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

`sign-release.ps1 -Thumbprint CERTIFICATE_THUMBPRINT` signs the release using a code-signing certificate and private key in the current user's Windows certificate store. It checks validity and trust, uses SHA-256 with a timestamp, and verifies the resulting signature. A Windows SDK SignTool installation is required. Recreate the portable ZIP using the signed EXE before publishing. The script has been syntax-checked; actual signing has not been tested because no signing certificate is installed.
