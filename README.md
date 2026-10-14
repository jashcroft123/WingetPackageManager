The smallest measured GUI and TUI are included at the repository root:

| App | Language | EXE | Build |
|---|---|---|---|
| Native GUI, 101,888 bytes | C++ | `WingetSwift.exe` | `powershell -File build.ps1` |
| Smallest native GUI, 43,008 bytes | C | `WingetSwiftGuiC.exe` | `powershell -File build-native-c.ps1` |
| Native TUI, 46,592 bytes | C | `WingetSwiftTuiC.exe` | `powershell -File build-tui-c.ps1` |

All three source implementations and build scripts are on main. See [C GUI notes](NATIVE-C.md), [console controls](TUI-C.md) and [size measurements](TUI-COMPARISON.md). These are unsigned experimental binaries; security status remains unresolved.

# Winget Swift

A small native Windows GUI for Microsoft WinGet. The default build uses ordinary Windows buttons, edit controls, a virtual package table and native scrollbars. No custom drawing, GUI framework, browser engine, bundled fonts or extra runtime installer.

The x86 EXE is **101,888 bytes (101.9 KB / 99.5 KiB)**, down from 185,344 bytes in the styled build. See [size measurements and validation](NATIVE.md).

**Security status remains unresolved.** Earlier downloads were repeatedly blocked; a screenshot identified `Trojan:Win32/Sabsik.FL.A!ml`. Windows Application Control also blocked a diagnostic build during this rewrite. Local Defender scans do not establish that those reports are false positives. The checked-in EXE is an unsigned experimental build, not a security-cleared release. Keep flagged files quarantined and do not add exclusions or disable protection. [Investigation and exact hashes](DEFENDER-REVIEW.md).

## Features

- **Remote:** search the community WinGet source by name or package ID, then install selected packages.
- **Installed:** filter locally and remove selected packages.
- **Updates:** list available upgrades and update selected packages.
- **Refresh:** bypass cached results and request a fresh listing.
- **Export setup:** write WinGet JSON with the identifiable installed packages and versions.
- **Import setup:** install the latest available versions from that JSON on another machine.

Ctrl-click selects separate rows; Shift-click selects a range. Ctrl+A or Select all selects every displayed row. Package changes require confirmation, use exact package IDs and run sequentially. Failures are logged; a timeout stops the remaining batch. Refreshing or changing filters clears selection to avoid stale targets.

Background WinGet work leaves navigation, filtering and selection available. The newest pending check replaces earlier requests, and obsolete read-only checks are canceled. Repeating a check already in progress reuses it; Refresh deliberately starts a fresh check. Results apply only to the matching view/query. Feedback identifies the operation, elapsed time and batch position; failures remain visible after the subsequent refresh.

Windows owns control rendering, scrolling, keyboard behavior and system/high-contrast colors. The window follows per-monitor DPI. Table columns fit the available width. Styling and custom dark-mode drawing are intentionally absent.

## Cache and portability

The window is created before any WinGet or network work. The table is virtual, so rows are not individual UI objects. Installed lists cache for 10 minutes, updates for 5 minutes and remote queries for 24 hours. Expired results display while refreshing. At most 128 cache files are retained, and package changes invalidate them.

Default cache location: `%LOCALAPPDATA%\WingetSwift\cache`. `WingetSwift.exe --portable` keeps the cache beside the EXE instead; that directory must be writable. Delete the cache directory to reset it.

Requires Windows 10/11 and Microsoft's App Installer/WinGet. WinGet's [requirements](https://learn.microsoft.com/en-us/windows/package-manager/winget/) include Windows 10 version 1809 or later. The x86 build runs on both x86 and x64 Windows. All imported DLLs are Windows components, including its built-in Universal C Runtime. No Visual C++ redistributable is required: the C++ and compiler runtime components are statically linked using [Microsoft's Hybrid CRT configuration](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/using-the-microsoft-c-runtime-with-user-mode-drivers-and-apps).

The app resolves WinGet only from the registered Microsoft App Installer package and verifies a trusted Microsoft Corporation signature. It ignores replacement executables in the current directory and PATH. Restart the app if an App Installer update removes the package directory it verified earlier.

Export covers packages WinGet can identify. It does not copy app data, settings, licenses or installers. Review JSON before importing. Missing packages and other warnings appear in the output. See Microsoft's [export](https://learn.microsoft.com/en-us/windows/package-manager/winget/export) and [import](https://learn.microsoft.com/en-us/windows/package-manager/winget/import) documentation.

## Build

Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Output: `dist/WingetSwift.exe` and `dist/native/WingetSwift.exe`. The build uses x86, size optimization and link-time optimization. ASLR, DEP, stack-security checks and executable trust verification remain enabled. No packer or obfuscator is used. `build-native.ps1 -Architecture amd64` produces the larger x64 variant.

The previous styled UI remains in `src/compact.cpp`, built with `build-compact.ps1`. The classic and Slint experiments remain buildable with `build-classic.ps1` and `build-modern.ps1`; their historical measurements are in COMPACT.md and BENCHMARKS.md.

## Verification

On a machine whose code-integrity policy permits the test build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Diagnostics
Start-Process .\dist\native-diagnostics\WingetSwift.exe --self-test -Wait
Start-Process .\dist\native-diagnostics\WingetSwift.exe --ui-test -Wait
Start-Process .\dist\native-diagnostics\WingetSwift.exe --live-test -Wait
powershell -NoProfile -ExecutionPolicy Bypass -File .\measure-native.ps1 -Trials 10
```

Tests are excluded from the release. Logs: `test-results.txt`, `native-ui-test.txt`, `native-live-test.txt`. Live tests search/list and create then remove a temporary export; they do not install, remove, update or import applications. `measure-native.ps1` measures the release EXE's launch-to-input-idle time and private memory, excludes one warm-up, and writes `native-launch-results.csv`. It has not been run against the final build because Application Control blocked diagnostic execution. No new launch-speed claim is made.

The parser is validated against English WinGet output. It handles UTF-8, progress/ANSI cleanup, repeated tables and CJK column widths. Localized headers and unusual Unicode widths may need adaptations. Truncated IDs cannot be used for package actions. Read operations time out after two minutes; package operations after one hour. An installer started by WinGet may continue after a timeout; inspect the output before retrying.

## Signing

The EXE is unsigned. `sign-release.ps1 -Thumbprint CERTIFICATE_THUMBPRINT` uses a trusted code-signing certificate from the current user's store, SHA-256 and a timestamp, then verifies the signature. No suitable certificate is installed on the build host. Signing establishes publisher identity; it does not prove that the reported malware classification is resolved. Recreate the ZIP after signing. See [Microsoft's reputation guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).
