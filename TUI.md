# Native console variant

Build with `build-tui.ps1`; add `-Diagnostics` for the separate diagnostic build. The default is x86 and `-Architecture amd64` builds x64. Release output is `dist/tui/WingetSwift-TUI.exe`. Run from a Windows console; `--portable` puts the shared-format cache beside the executable.

The first screen appears before WinGet starts. Press 1, 2 or 3 for remote, installed or update lists. `/` edits the remote search or instantly filters local lists. Use arrows/Page Up/Page Down, Space to check rows, Shift with movement or Space for a range, and Ctrl+A to select visible rows. Enter opens a confirmation containing stable command snapshots; y proceeds and n cancels. `r` refreshes, `l` shows captured output, and `q` exits when no package mutation is active.

`e` exports installed packages to JSON. `i` imports JSON after confirmation. Both prompt for a path, initially `installed-packages.json`; Ctrl+A clears it. WinGet can request elevation through Windows when an installer needs it.

The console uses Windows input events and a separate screen buffer. Network/process work runs on one worker thread. New read requests cancel obsolete work and retain the latest pending request; completion from an old view cannot replace current results. It reuses GUI cache keys and freshness periods, Microsoft-signed registered App Installer lookup, exact package IDs, command quoting, timeout and batch summaries. No PATH lookup, framework, packer or bundled runtime is used.

The diagnostic executable accepts `--self-test` and writes `tui-test-results.txt`. It tests parsing, quoting, selection, filtering, confirmation snapshots, path editing and timeout batch summaries without executing WinGet. Builds and static checks can be performed without running the executable. Runtime console behavior still requires validation where application control permits execution.

Shift selects the inclusive range from the last ordinary focus move or Space selection to the current row; it checks that range without toggling existing checks. Confirmation shows the first three exact IDs and the number of remaining targets. Export includes installed versions and does not invalidate or write listing caches. Import ignores saved versions. Failed package operations keep a warning and their output through the subsequent list refresh. Browsing during a mutation queues the latest requested listing without canceling the mutation.

Filtering clears checked rows. Truncated package IDs are rejected. The release artifact is also copied to dist/WingetSwiftTui.exe and packaged with documentation. See [measured sizes and validation limits](TUI-COMPARISON.md).
