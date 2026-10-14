# Compact native UI

The recommended build is now **Winget Swift Compact**: the modern layout recreated with native Win32 controls and GDI drawing. It has no Slint, browser engine, bundled fonts, or additional runtime DLLs.

The current release EXE is **179,200 bytes** (179.2 decimal KB, 175 KiB), down from 467,456 bytes in the previous delivered build. Minimum size is the target. The default x86 build is the smallest variant; x64 is also available. Both use static CRT linkage and only Windows system DLLs. Existing features remain: remote search/install, cached installed and updatable lists, Ctrl/Shift multi-selection, sequential batch actions, and WinGet JSON export/import.

The UI includes a sidebar, rounded buttons/search field, spacious rows, selection counts, and a collapsible activity panel. It follows Windows light/dark app settings and system high-contrast colors. Controls retain native keyboard interaction; Ctrl+A selects all package rows.

## Matched measurements

These historical measurements are for the 458,752-byte build before the scrollbar styling change. The new release removes diagnostic code and uses Win32 cache/file APIs, manual line parsing, and a small native worker wrapper. Scrollbar painting keeps native scrolling; its repaint timer runs only during thumb tracking. High contrast uses Windows' default scrollbar rendering.

| Measurement | Classic | Slint trial | Compact native |
|---|---:|---:|---:|
| EXE bytes | 492,544 | 13,955,072 | **458,752** |
| Warm launch median | 86.90 ms | 157.47 ms | **79.48 ms** |
| Warm launch range, 10 runs | 84.53–122.93 ms | 112.26–230.70 ms | **77.18–89.20 ms** |
| Private memory median | 2.01 MiB | 9.12 MiB | **2.01 MiB** |
| Resident working set median | 11.08 MiB | 26.49 MiB | **10.86 MiB** |
| Idle CPU median, one core | 0% | 0% | **0%** |
| Idle CPU mean, one core | 0% | 0.16% | **0.16%** |
| Maximum 2-second idle CPU sample | 0% | 0.78% | **0.78%** |

Compact is approximately **96.7% smaller than the Slint EXE**. Its measured startup was comparable to the original native build, and faster in this pass. These are results for these specific binaries on this machine. The compact build also uses size optimization and link-time optimization (`/O1 /GL /LTCG`); the comparison is not an isolated test of UI frameworks.

All three were measured in one quiet pass on the same Intel i5-8600K, Windows build 26200, 125% display scaling, with ten alternating launches after one excluded warm-up each. Each loaded the same 98-package fixture, created a hidden native window with a 1375×950 physical-pixel viewport, populated its list, rendered offscreen, and signaled readiness. Timing starts externally at process creation and includes loader time. No WinGet/network work runs during the benchmark.

Memory and CPU are sampled after 750 ms settling, over a two-second idle window. CPU-time granularity is approximately 15.625 ms, so zero samples do not establish zero ongoing CPU use. These are warm-cache, instrumented offscreen-readiness measurements, not cold-launch or desktop compositor presentation measurements. Native builds use GDI printing and Slint uses its renderer snapshot. Scrolling and package-operation speed were not benchmarked.

Raw data: `compact-benchmark-results.csv`; machine/method summary: `compact-benchmark-summary.json`. Reproduce from the workspace:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\benchmark.ps1 -Trials 10 -IncludeCompact -OutputPrefix compact-benchmark
```

## Build and validation

`build.ps1` now builds compact as the default and copies it to `dist/WingetSwift.exe`. `build-compact.ps1` writes `dist/compact/WingetSwift.exe`. `build-classic.ps1` retains the previous UI as `dist/WingetSwiftClassic.exe`; `build-modern.ps1` retains the Slint trial.

Parser, quoting, cache and batch failure/timeout regressions pass. Native multi-selection, select-all/clear action state, filtering, activity expansion, light/dark rendering, high-contrast priority, live theme notification and GDI resource cleanup tests pass. Both light and dark screenshots were visually checked. Read-only live checks pass for remote, installed, updatable and empty lists, and a temporary migration export. Real package installation, update, removal and import were not executed.

The standalone EXE imports only standard Windows DLLs; WinGet remains required. There are no accompanying UI assets to keep beside the EXE. Native dialogs/scrollbars retain Windows appearance. Package parsing retains the English WinGet output and unusual Unicode-width limitations documented in README.md.

Layout validation covers 48 combinations of 900/1100/1400 logical-pixel widths, 600/760 heights, 100/125/150/200% scaling, and expanded/collapsed activity panels. Narrow windows place the selection actions on a separate row. Controls and fonts update when moving between displays with different DPI.

Background work leaves browsing and selection enabled. Requests are serialized: the latest pending check replaces earlier requests, obsolete read-only checks are canceled, and completion results apply only to their matching view/search. Regression checks cover enabled controls, filtering while busy, request replacement/cancellation, and stale result isolation.

## Current release measurements

Ten warm launches after an excluded functional-check run measured median launch-to-responsive-window times of **67.96 ms for x86** and **60.92 ms for x64**. `release-smoke.ps1` measures the shipped EXEs directly and checks update feedback and navigation during background work. Raw results are in `release-launch-results.csv`. This endpoint is window creation/input readiness, unlike the historical fixture-render benchmark above; it does not wait for WinGet results or desktop compositor presentation.

The default release excludes diagnostic commands. `build.ps1 -Diagnostics` builds `dist/diagnostics/WingetSwift.exe` with the same app code plus tests. The historical benchmark script now uses that diagnostic EXE, so build it before running the benchmark. `build-compact.ps1 -Architecture amd64` builds `dist/compact-x64/WingetSwift.exe`; add `-Diagnostics` for `dist/diagnostics-x64/WingetSwift.exe`.

Diagnostics use a synthetic 98-package fixture unless a local `benchmark-fixture.txt` is provided. The original machine's installed-package inventory is excluded from Git. Historical fixture-render measurements used that local inventory; fresh benchmarks with the synthetic fixture use different data.

Update feedback includes elapsed time, animated activity, and the current package's position in a batch. Animation stops on completion. Rounded surfaces use [Windows GDI+ antialiasing](https://learn.microsoft.com/en-us/windows/win32/api/_gdiplus/) without bundling a drawing runtime. Regression checks verify blended edge pixels and timer cleanup.

Cache tests cover legacy files, Unicode paths, malformed/overflowing timestamps, maximum captured output, 128-entry pruning, and invalidation. Read-only live tests pass on x86 for remote search, installed/updatable lists, empty searches, and migration export. Real package installation/update/removal/import were not executed.

Resize painting now batches child positions, avoids copying stale pixels, buffers the parent background, and skips layout while minimized. Columns update only when their available width changes and reserve scrollbar space. Checks cover 120 resize steps across the toolbar breakpoint, both activity states, column fit without horizontal scrolling, minimization, and the existing 48 size/DPI combinations. The launch measurements above predate this resize fix, which adds 1,536 bytes to the x86 EXE; launch time has not been re-measured.
