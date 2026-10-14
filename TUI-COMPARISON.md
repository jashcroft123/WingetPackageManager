# Native size comparison

Measured 2026-10-14, MSVC x86 release builds; raw EXE bytes, without ZIP compression.

| Variant | Branch | EXE bytes | KiB |
|---|---|---:|---:|
| C++ native GUI | main, 2157c17 | 101,888 | 99.5 |
| C++ native console | experiment/native-tui | 96,768 | 94.5 |
| C native console | experiment/c-tui | 46,592 | 45.5 |
| C native GUI | main | **43,008** | **42.0** |

The C GUI is now smallest: 57.8% below the C++ GUI and 7.7% below the C TUI. The C TUI is 54.3% below the C++ GUI and 51.9% below the C++ console. Changing only the frontend saves little; removing the C++ standard-library backend accounts for most of the reduction. The C versions use Windows heap allocations, native controls or console APIs, without a framework. All variants require Windows and Microsoft App Installer/WinGet. Windows console hosting and system UCRT are not bundled. ASLR, DEP, stack protection, Microsoft signer verification and an asInvoker manifest remain enabled; no packer is used.

## Validation and limits

Release and separate diagnostic builds compile. MSVC /analyze reports only approximately 64 KiB stack-buffer warnings. PE inspection confirms x86, ASLR/DEP and Windows/system UCRT imports. The C /Ob0, /Ob2 and /Os probes all produce 46,592 bytes, so those flags do not improve size.

No new executable or diagnostic tests were run: Windows Application Control blocked a previous diagnostic build, and the earlier Trojan:Win32/Sabsik.FL.A!ml report remains unresolved. Runtime behavior, real WinGet integration, memory usage and launch timings require a policy-approved environment. Compiling diagnostic tests is not a passing test run. English WinGet table headers are currently required; output capture is bounded at 16 MiB, so unusually large results can be incomplete. These are unsigned experimental artifacts, not security-cleared releases.

Non-remediating local Defender scans of both final console EXEs found no threats on 2026-10-14, engine 1.1.26090.9, signatures 1.461.27.0, real-time protection enabled. This does not disprove earlier reports or establish trust on another machine. Keep flagged files quarantined; do not bypass protection.

| Artifact | SHA-256 |
|---|---|
| WingetSwiftTui.exe | B280274BD5FE43061E71347A4478EBC03DFFA9BE2A282910940C2A62547624C9 |
| WingetSwiftTuiC.exe | A1F07333E3C43BCFFCE968DF9A56C4F9CCA86C3F7EAA2B99E9871ED1192DF813 |

The native GUI implementation remains unchanged. The C TUI source has also been integrated into main; the smallest measured GUI and TUI EXEs are at the repository root. Both independent experiment branches remain available.

## Top-level builds integrated into main

Rebuilt 2026-10-14 from the integrated source with the default x86 build scripts. PE timestamps mean rebuild hashes can differ from the earlier experimental artifacts; sizes remain unchanged. Both top-level EXEs received non-remediating local Defender scans with no threats found. Runtime validation and the earlier security reports remain unresolved.

| Top-level file | Bytes | SHA-256 |
|---|---:|---|
| WingetSwift.exe | 101,888 | BEC8F093466299352496B76E04B435A2F14A5D1FB8C96862B4688151727D0EAB |
| WingetSwiftTuiC.exe | 46,592 | 21297DACF63856980354AA45331CB1E8AA8B980DED7C9ADD63A5B604B98458A0 |

C GUI final build and hash evidence: [NATIVE-C.md](NATIVE-C.md). Its diagnostic build is 53,760 bytes. The top-level C GUI received a non-remediating clean local Defender scan; runtime verification remains pending.
