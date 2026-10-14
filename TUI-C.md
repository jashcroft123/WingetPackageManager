# Native C console experiment

Build with `powershell -NoProfile -ExecutionPolicy Bypass -File build-tui-c.ps1`. Visual Studio Build Tools with the C++ desktop workload is required. Default x86 output: `dist/tui-c/WingetSwiftTuiC.exe`; add `-Architecture amd64` for x64. The build also creates the checked-in top-level x86 copy `WingetSwiftTuiC.exe` (or `WingetSwiftTuiC-x64.exe` for x64); its portable ZIP contains the same EXE and documentation. Run from a Windows console. `--portable` stores caches beside the EXE; otherwise caches use LocalAppData.

No console framework or C++ standard library. Win32 console events and a separate screen buffer use the terminal's palette. WinGet runs on one worker thread, with busy feedback, bounded capture/timeouts and captured exit codes. Cached lists appear before network refresh. Obsolete read requests are canceled; the latest pending request is retained. Package changes run sequentially and cannot be canceled by navigation or ordinary quit. Failed operations and their logs survive the subsequent listing refresh. Export uses WinGet JSON with versions; import installs current available versions. Exact IDs and sources are copied before confirmation. Truncated IDs are rejected. Updates use `list --upgrade-available` without `--include-pinned`, following WinGet's default pin filtering.

| Key | Action |
|---|---|
| 1 / 2 / 3 | Remote / installed / updates |
| / | Search remotely or filter the local list |
| Arrows, PgUp/PgDn, Home/End | Move focus |
| Space | Toggle focused row |
| Shift + movement/Space | Replace selection with the inclusive anchored range |
| Ctrl+A | Select every filtered row, including off-screen rows |
| Enter | Review stable exact command targets |
| Up/Down, PgUp/PgDn in confirmation | Scroll all commands, including sources and paths |
| Y / N | Apply / cancel confirmation |
| R | Refresh, bypassing freshness |
| E / I | Enter export / import JSON path |
| L | Toggle output log; movement keys scroll it |
| Q / Ctrl+C | Quit when no mutation is active |

Path editing supports arrows, Home/End, Delete/Backspace and Ctrl+A to clear. Enter submits and Escape cancels. Refreshing, changing views or filters clears selection. Import requires confirmation. Avoid forcibly closing the console during an installer; inspect running installers if it is forcibly closed. Standard console input mode, title and screen buffer are restored on ordinary exit.

`-Diagnostics` produces a separate diagnostic executable. It accepts `--self-test` (parser, quoting, cache and command tests) and `--ui-test` (selection/filter state tests); neither intentionally invokes WinGet. They have compiled but have not run because runtime validation is pending. Real console resizing, keyboard input and WinGet operations also remain unverified. See [size measurements, hashes and security limits](TUI-COMPARISON.md).
