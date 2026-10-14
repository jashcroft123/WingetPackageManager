# TUI size experiment

Measured 2026-10-14, MSVC x86 release builds; raw EXE bytes, without ZIP compression.

| Variant | Branch | EXE bytes | KiB |
|---|---|---:|---:|
| C++ native GUI | main, 2157c17 | 101,888 | 99.5 |
| C++ native console | experiment/native-tui | 96,768 | 94.5 |
| C native console | experiment/c-tui | 46,592 | 45.5 |

C is the smallest: 54.3% below the GUI and 51.9% below the C++ console. Changing only the frontend saves little; removing the C++ standard-library backend accounts for most of the reduction. The C rewrite uses Windows heap allocations and console APIs, without a TUI library. All variants require Windows and Microsoft App Installer/WinGet. Windows console hosting and system UCRT are not bundled. ASLR, DEP, stack protection, Microsoft signer verification and an asInvoker manifest remain enabled; no packer is used.

## Validation and limits

Release and separate diagnostic builds compile. MSVC /analyze reports only approximately 64 KiB stack-buffer warnings. PE inspection confirms x86, ASLR/DEP and Windows/system UCRT imports. The C /Ob0, /Ob2 and /Os probes all produce 46,592 bytes, so those flags do not improve size.

No new executable or diagnostic tests were run: Windows Application Control blocked a previous diagnostic build, and the earlier Trojan:Win32/Sabsik.FL.A!ml report remains unresolved. Runtime behavior, real WinGet integration, memory usage and launch timings require a policy-approved environment. Compiling diagnostic tests is not a passing test run. English WinGet table headers are currently required; output capture is bounded at 16 MiB, so unusually large results can be incomplete. These are unsigned experimental artifacts, not security-cleared releases.

Non-remediating local Defender scans of both final console EXEs found no threats on 2026-10-14, engine 1.1.26090.9, signatures 1.461.27.0, real-time protection enabled. This does not disprove earlier reports or establish trust on another machine. Keep flagged files quarantined; do not bypass protection.

| Artifact | SHA-256 |
|---|---|
| WingetSwiftTui.exe | B280274BD5FE43061E71347A4478EBC03DFFA9BE2A282910940C2A62547624C9 |
| WingetSwiftTuiC.exe | A1F07333E3C43BCFFCE968DF9A56C4F9CCA86C3F7EAA2B99E9871ED1192DF813 |

The main GUI remains unchanged. Experiments are independent branches, each retaining the GUI baseline sources and artifact.
