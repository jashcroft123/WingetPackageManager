# Minimal native build

The default frontend is `src/native.cpp`: standard Win32 controls and a virtual common-controls report list. Windows renders the buttons, table/header, selection and scrollbars. The custom sidebar, rounded drawing, GDI+, palette rendering, custom scrollbar, animation band, custom row heights and drawing subclasses are absent. Functional progress feedback remains.

The portable EXE imports only Windows DLLs. It uses static C++/vcruntime with Windows' system UCRT, following [Microsoft's documented Hybrid CRT configuration](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/using-the-microsoft-c-runtime-with-user-mode-drivers-and-apps). It has no MSVCP140 or VCRUNTIME140 DLL dependency. ASLR, DEP, stack-security checks and Microsoft-signed WinGet resolution remain enabled.

## Size measurements

Actual uncompressed x86 EXE lengths measured with Visual Studio 2022 MSVC on Windows 11; raw sizes are in `native-size-results.csv`:

| Build / isolated probe | Bytes |
|---|---:|
| Previously delivered styled compact build | 185,344 |
| First plain controls frontend, fully static CRT | 167,424 |
| First plain frontend, Hybrid CRT | 101,376 |
| Final native build, including review fixes | **101,888** |

Final size: **101.9 decimal KB / 99.5 KiB**, a **45.0% reduction** from the delivered styled build. DLLs already supplied with Windows are not counted as bundled files; the distribution has one EXE. WinGet itself remains a separate prerequisite.

Isolated compiler probes on an earlier native iteration found `/Oi` saved 512 bytes; `/Ob0` increased size, `/Ob2`, `/Os` and additional ICF passes did not help. x64 was 139,264 bytes at that stage, versus x86 100,864. The final code includes additional async/error/DPI fixes, so those probe sizes are not final artifact sizes. `/MD` was rejected because it needed separately installed Visual C++ DLLs. No packing or security-feature removal was used.

## Validation and remaining limits

The final release and diagnostic builds compile. Binary inspection confirms x86, ASLR/DEP and Windows/UCRT-only imports. MSVC static analysis reports two large-stack warnings for bounded 32,768-character path/file-dialog buffers (about 64 KiB each); these remain within the default 1 MiB stack. Independent Opus review and a focused Sol follow-up checked the async state and lifecycle. Their actionable findings were addressed: active checks are reused, superseded cancellation avoids false errors, mutation failure status survives refresh, initial size is DPI-scaled, and the radio group ends before Export.

An earlier prototype's backend self-tests passed: trusted executable resolution despite hostile PATH/current directory, quoting, parser, cache boundary/pruning/invalidation and batch failure/timeout behavior. Its UI checks passed multi-selection, local filtering, background feedback, request replacement, stale-result isolation, native wheel/end scrolling and 24 size/DPI layouts. A preview was rendered and visually inspected. Two diagnostics initially failed because a synthetic native thumb message did not set Windows' internal track position and the GDI comparison ran before native-control warmup. The checks were corrected, but the updated diagnostic build could not execute.

**Final runtime verification is pending.** Windows Application Control rejected the rebuilt diagnostic EXE with events 3077/3033: it did not meet signing-level/code-integrity requirements. Policy ID: `{0283ac0f-fff1-49ae-ada1-8a933130cad6}`. No protection settings were changed and no bypass attempted. Final self/UI/live checks and release launch/memory measurements need a signed or policy-approved build environment. The benchmark script is provided, but no new launch timing is claimed. Actual package mutations have not been tested.

The earlier reported Trojan detection also remains unresolved. A local clean Defender scan is not a malware-analysis verdict. Exact sample hashes and scan evidence are tracked in DEFENDER-REVIEW.md.
