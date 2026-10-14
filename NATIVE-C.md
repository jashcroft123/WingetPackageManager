# Pure C native GUI

`src/native_c.c` uses standard Win32 buttons, edit controls, file dialogs and an owner-data common-controls list. It shares `src/tui_core.c` with the C TUI: parser, cache, immutable exact package commands, quoted process launch and Microsoft-signed registered App Installer resolution. There is no GUI framework, web engine, C++ runtime library or packer.

Build the x86 release with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-native-c.ps1
```

The build produces a top-level `WingetSwiftGuiC.exe`. Add `-Architecture amd64` for a separately named x64 build. `-Analyze` enables MSVC static analysis; `-Diagnostics` builds separately with the core test entry point. Build Tools for Visual Studio with the desktop C++ workload provides the C compiler and Windows SDK.

Remote searches query WinGet; installed and update filters operate locally. Ctrl-click/Shift-click use Windows' native multi-selection; Ctrl+A selects displayed rows. Package operations use snapshots of the exact IDs and sources and require confirmation. Refresh/filter/view changes clear selection. Export produces WinGet JSON including identifiable installed versions; import requests current available versions. WinGet and installers can request elevation through Windows.

Work runs on one background thread. Obsolete reads can be canceled and replaced by the latest request; package mutations continue sequentially, with progress and captured output. Ordinary close is refused while changes are running. Native system fonts/colors and per-monitor DPI are used; the table owns its scrolling. No custom painting or scrollbar is introduced.

## Validation limits

The earlier Trojan:Win32/Sabsik.FL.A!ml report and Windows Application Control block remain unresolved. These unsigned artifacts are experimental. Local scans cannot establish that previous reports were false positives. Do not disable protection or add exclusions for flagged files.

Runtime GUI, DPI/resize/scroll behavior, real WinGet integration and launch performance need a policy-approved environment. Diagnostic compilation is not a test pass. English WinGet table headers are currently required, and captured output is bounded. Size and final static/scan evidence are recorded below.

## Final measured build

The x86 C GUI EXE is **43,008 bytes (42.0 KiB)**: 57.8% smaller than the C++ GUI (101,888 bytes) and 7.7% smaller than the C TUI (46,592 bytes). Source, build script and top-level EXE are included on main; the C++ GUI remains available for comparison.

Release and separate diagnostics compile successfully (diagnostics: 53,760 bytes). MSVC /W4 and /analyze report no GUI warnings; the unchanged C backend reports one approximately 64 KiB stack-buffer warning. A narrowly documented C28112 suppression covers passing the cancel pointer to the separately compiled backend, which accesses it through Interlocked operations. PE inspection confirms x86, ASLR/DEP and Windows/system UCRT imports. Independent Opus review findings were addressed: buttons re-enable after canceled/failed reads, initial size scales with system DPI, read failure reasons/output are shown, and timeout warnings persist through refresh. Batch logs grow geometrically and retain a bounded 16 MiB of output with truncation and success/failure/not-attempted summaries.

No new EXE or diagnostic tests were executed. A non-remediating local Defender scan on 2026-10-14 found no threats with engine 1.1.26090.9, signatures 1.461.27.0, real-time protection enabled. This is not security clearance or proof that earlier detections were false positives.

SHA-256 of the top-level `WingetSwiftGuiC.exe`:

```text
A5C9A9181F9D17AE106ECF3FD67F2DA20CFCC69FDD38A11951D280F64ED46E37
```

Closing during a read/export cancels it; an interrupted export may leave partial JSON and should be recreated. Forced closure during a mutation is outside the ordinary shutdown path; an installer may continue. GUI layout/scroll behavior and launch speed remain unmeasured at runtime.
