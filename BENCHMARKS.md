# Classic vs Slint

The modern Slint version is a usable alternative, with the same shared C++ WinGet runner, parser, disk cache, migration commands, and sequential package operations. It adds a sidebar, Fluent text inputs, rounded actions, selection counts, accessible package rows, and a collapsible activity panel. Both are standalone unsigned Windows x64 EXEs requiring WinGet.

| Measurement | Classic Win32 | Modern Slint 1.18 |
|---|---:|---:|
| Launch to fixture loaded and rendered, median | 88.18 ms | 178.06 ms |
| Launch range, 10 runs | 84.30–97.62 ms | 131.11–214.50 ms |
| Private memory, median | 2.00 MiB | 9.12 MiB |
| Resident working set, median | 11.08 MiB | 26.50 MiB |
| Idle CPU, median (% of one core) | 0% | 0% |
| Idle CPU, mean (% of one core) | 0% | 0.39% |
| Idle CPU, maximum 2-second sample | 0% | 2.33% |
| EXE size | 492,544 bytes (0.47 MiB) | 13,955,072 bytes (13.31 MiB) |

Slint added approximately **90 ms** to median launch and **7.12 MiB** of private memory. The launch median was about **2.02×** the classic version; the EXE is about **28.3×** larger. These are measurements of these specific builds on this machine, not general claims about C++ or Slint.

For this utility, the modern version remains practical: a warm launch under 0.2 seconds at the median, modest memory use, and low idle CPU use. Seven of ten Slint samples recorded no CPU delta; three recorded small bursts (15.625–46.875 ms CPU time). All classic samples recorded no CPU delta. Keep the classic version if the smallest download and minimum startup overhead matter most. Both use the same WinGet backend; package-operation performance was not benchmarked.

## Method and limits

Measured on an Intel Core i5-8600K (6 logical processors), Windows build 26200, 125% display scaling. Both use static runtimes, optimized release builds, and Windows DLLs only. Slint uses Winit + software rendering, ahead-of-time generated C++, embedded assets, and accessibility support; Qt, browser engines, GPU renderers, interpreter, and system tray are disabled.

Ten recorded launches per app, in alternating order, after one excluded warm-up each. No builds or tests ran alongside the final recorded pass. Both load the same 98-row `benchmark-fixture.txt`, without calling WinGet or using the network. Both create a real native window, explicitly hide it, populate their package models, and force an offscreen render before signaling a per-process readiness event. Native HWNDs, hidden visibility, fixture count, and matched client sizes are checked. The 1100×760 logical viewport is 1375×950 physical pixels on this display. A small diagnostic-file write is included equally in both readiness paths.

Launch time is measured externally from `Process.Start` until readiness, including process creation/loader time. It is a warm-filesystem, instrumented UI-readiness measurement; it is not a cold-boot measurement or the time until the desktop compositor presents a visible window. The earlier ~38 ms figure measured only in-process window initialization and is not comparable to this test.

After 750 ms of settling, idle CPU is sampled for two seconds and memory is read. CPU is expressed as percentage of one logical core, with a measured ~15.625 ms CPU-time granularity. No observed CPU delta in a short sample does not prove zero CPU use indefinitely. Working set includes shared/code pages; private memory is reported separately. Antivirus, scheduling, display settings, and hardware can change the results. Scrolling/animation responsiveness and cold launch have not been benchmarked.

Raw final results: `benchmark-results.csv`; machine/method summary: `benchmark-summary.json`.

## Reproduce

Build the classic and modern versions, copy `dist/WingetSwift.exe` to `dist/WingetSwiftClassic.exe`, and keep the included 98-row fixture in the working directory. Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\benchmark.ps1 -Trials 10
```

The script creates diagnostic `perf-state-*.txt` files, launches only these app builds, and closes each app through its actual HWND. A timeout terminates the benchmark process it started. It does not install, update, or remove packages.

## Validation

- Shared parser, quoting, cache and batch failure/timeout regressions pass in the classic build.
- Classic UI selection and theme tests pass after backend extraction.
- Modern callback tests pass for Ctrl-toggle, Shift-range, filtering, select-all and clearing selection.
- Modern light/dark snapshots were rendered and visually checked. Native-window creation and live Windows theme-hook tests pass.
- Modern asynchronous installed-package refresh passes against this machine (98 packages).
- Native WinGet export was verified earlier and its shared command path is retained. Real install, update, removal and import operations were not executed.

The modern build follows the Windows app light/dark preference. High contrast disables its dark mode, but full custom high-contrast palette mapping remains a limitation of this trial; the classic build retains its full Windows high-contrast palette. Both keep their limitations around localized WinGet table formats and truncated IDs, as documented in README.md.
