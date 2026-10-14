# Defender detection investigation

Status on 14 October 2026: **reported detection unresolved**. A user screenshot shows `Trojan:Win32/Sabsik.FL.A!ml` (Severe, Active). Its affected file path and hash are unavailable, so it cannot yet be matched to either binary below. No Microsoft analysis verdict has been received. A clean local scan does not establish that the alert is a false positive.

## Changes and validation

The source review found an executable lookup weakness: searching for `winget.exe` could select a replacement in an untrusted directory. The hardened build resolves WinGet exclusively from the registered `Microsoft.DesktopAppInstaller_8wekyb3d8bbwe` package and requires a trusted Microsoft Corporation Authenticode signature. It rejects unsigned replacements and does not search the current directory or PATH. Verification runs on the background worker and is cached per process.

Regression coverage puts a fake `winget.exe` in both the current directory and PATH. The genuine signed App Installer executable is selected and the fake is rejected. Parser/cache/batch tests, native UI tests, and read-only live remote/installed/update/export checks pass. Actual package mutations were not performed.

The earlier and initial hardened binaries passed local non-remediating Microsoft Defender custom scans with exit code 0 and no threats found. Real-time protection remained enabled. Security intelligence: `1.461.20.0`; engine: `1.1.26090.9`. The earlier binary was scanned twice. The reported alert was not reproduced on the build host.

| Binary | Bytes | SHA-256 |
|---|---:|---|
| Earlier published EXE, commit `a61d3bb` | 182,784 | `3023E8397551A1242034FDDEAC4C700F6943675A468FD8C28D70AE81BDC30D61` |
| Initial hardened EXE, commit `3e00cf9` | 185,344 | `9B8CC23CBB8622B0D2867643BBAB225329DF5829CA48906B418508301B5F194B` |
| Current EXE with scrollbar drag fix | 185,344 | `71D00B8D9A191A90E547DBE89B808926260CBD9FBCAE7588370E827798004FBC` |

The current drag-fix EXE also passed a non-remediating Defender scan with no threats and exit code 0, using intelligence `1.461.27.0` and engine `1.1.26090.9`, with real-time protection enabled. This does not resolve the user's reported classification.

All three EXEs are unsigned. Compiler/linker and the resolved WinGet executable have valid Microsoft signatures. The compact build uses MSVC with a static CRT and Windows system DLLs; it uses no executable packer or obfuscator. This hardening change is not evidence that the original Trojan classification has been corrected.

## Prepared Microsoft review

The [Microsoft submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission) requires account sign-in for the Software developer route. No sample has been uploaded and no report submitted. Local review materials retain all three binaries separately so the earlier sample is not lost during replacement.

Suggested additional information (submit each sample separately):

> Please analyze a reported possible incorrect detection of Winget Swift, an open-source native Win32 GUI for Microsoft WinGet: https://github.com/jashcroft123/WingetPackageManager. A user screenshot reports Trojan:Win32/Sabsik.FL.A!ml, but does not identify the affected file or hash. The app searches package sources, lists installed/updatable packages, invokes WinGet for user-confirmed package changes, and supports JSON export/import. It runs asInvoker, uses redirected child-process output, and writes local caches. It does not install persistence or alter antivirus settings. The compact executable is built with MSVC, a static CRT and Windows DLLs; no packer or obfuscator is used. Both the earlier 182784-byte and hardened 185344-byte builds passed local Defender custom scans with intelligence 1.461.20.0 and engine 1.1.26090.9. The hardened build fixes untrusted executable lookup by requiring WinGet from the registered Microsoft App Installer package with a trusted Microsoft Corporation signature. This change has not been shown to explain the reported detection. Please determine whether the submitted sample is incorrectly classified.

Keep protection enabled and use the recommended quarantine/remediation action for the flagged file while its identity and classification remain unresolved. Do not add an exclusion to run it.
