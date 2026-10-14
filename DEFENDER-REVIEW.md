# Defender detection investigation

Status on 14 October 2026: **reported detection unresolved; README download links removed**. The user reports another virus block after the scrollbar drag-fix release. An earlier screenshot shows `Trojan:Win32/Sabsik.FL.A!ml` (Severe, Active). Affected file paths/hashes and the latest alert's detection name are unavailable, so the alerts cannot yet be matched conclusively to the samples below. No Microsoft analysis verdict has been received. A clean local scan does not establish that an alert is a false positive. Previously committed binaries remain in this repository and its history and are covered by the same warning; history has not been rewritten.

## Changes and validation

The source review found an executable lookup weakness: searching for `winget.exe` could select a replacement in an untrusted directory. The hardened build resolves WinGet exclusively from the registered `Microsoft.DesktopAppInstaller_8wekyb3d8bbwe` package and requires a trusted Microsoft Corporation Authenticode signature. It rejects unsigned replacements and does not search the current directory or PATH. Verification runs on the background worker and is cached per process.

Regression coverage puts a fake `winget.exe` in both the current directory and PATH. The genuine signed App Installer executable is selected and the fake is rejected. Parser/cache/batch tests, native UI tests, and read-only live remote/installed/update/export checks pass. Actual package mutations were not performed.

The earlier and initial hardened binaries passed local non-remediating Microsoft Defender custom scans with exit code 0 and no threats found. Real-time protection remained enabled. Security intelligence: `1.461.20.0`; engine: `1.1.26090.9`. The earlier binary was scanned twice. The reported alert was not reproduced on the build host.

| Binary | Bytes | SHA-256 |
|---|---:|---|
| Earlier published EXE, commit `a61d3bb` | 182,784 | `3023E8397551A1242034FDDEAC4C700F6943675A468FD8C28D70AE81BDC30D61` |
| Initial hardened EXE, commit `3e00cf9` | 185,344 | `9B8CC23CBB8622B0D2867643BBAB225329DF5829CA48906B418508301B5F194B` |
| Previously published EXE with scrollbar drag fix | 185,344 | `71D00B8D9A191A90E547DBE89B808926260CBD9FBCAE7588370E827798004FBC` |
| Current experimental plain-native EXE | 101,888 | `8AFA6FC8073223B53AFE4DA1E3DE56CF0F51F45C7A5197BFEC83E0ED7436477A` |

The current drag-fix EXE also passed a non-remediating Defender scan with no threats and exit code 0, using intelligence `1.461.27.0` and engine `1.1.26090.9`, with real-time protection enabled. This does not resolve the user's reported classification.

Following the repeated block report, signature update was requested and the exact published drag-fix EXE was scanned again: same SHA-256, intelligence `1.461.27.0`, no threats found. The local threat history has no matching Winget Swift detection. The repeated user report remains unresolved. No replacement build is being offered as a workaround.

All samples are unsigned. Compiler/linker and the resolved WinGet executable have valid Microsoft signatures. The previous compact builds use MSVC with a static CRT; the current native build uses static C++/compiler runtime components with Windows' built-in UCRT. All imports are Windows components. No executable packer or obfuscator is used. The size-focused rewrite is not evidence that the original Trojan classification has been corrected.

## Plain-native build and Application Control

The user requested a smaller, fully native frontend without custom styling. The experimental EXE is 101,888 bytes; its exact SHA-256 is above. Its local non-remediating Defender scan found no threats, exit code 0, with intelligence `1.461.27.0` and engine `1.1.26090.9`. Real-time protection remained enabled.

During development, Windows Application Control blocked a rebuilt diagnostic EXE. Events 3077 and 3033 reported that it did not meet signing-level/code-integrity requirements, policy ID `{0283ac0f-fff1-49ae-ada1-8a933130cad6}`. That blocked diagnostic sample's SHA-256 was `7357A86615B7DD4F8C6B865048D7A7195D3D19B2E3E34F1030F2F8D5D10146F6`. This is a distinct observed block from the earlier screenshot's Trojan classification. No settings were weakened and no exclusion/bypass was added. Further executable tests were stopped. Final source builds and binary/static checks were completed; final UI/live checks and launch measurements remain pending in a signed or policy-approved environment.

The current EXE and ZIP are included as unsigned experimental artifacts at the user's request, with explicit warnings. They are not represented as cleared or confirmed safe by Microsoft.

## Prepared Microsoft review

The [Microsoft submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission) requires account sign-in for the Software developer route. No sample has been uploaded and no report submitted. Local review materials retain earlier binaries separately so samples are not lost during replacement.

Suggested additional information (submit each sample separately):

> Please analyze a reported possible incorrect detection of Winget Swift, an open-source native Win32 GUI for Microsoft WinGet: https://github.com/jashcroft123/WingetPackageManager. A user screenshot reports Trojan:Win32/Sabsik.FL.A!ml, but does not identify the affected file or hash. The app searches package sources, lists installed/updatable packages, invokes WinGet for user-confirmed package changes, and supports JSON export/import. It runs asInvoker, uses redirected child-process output, and writes local caches. It does not install persistence or alter antivirus settings. The compact executable is built with MSVC, a static CRT and Windows DLLs; no packer or obfuscator is used. Both the earlier 182784-byte and hardened 185344-byte builds passed local Defender custom scans with intelligence 1.461.20.0 and engine 1.1.26090.9. The hardened build fixes untrusted executable lookup by requiring WinGet from the registered Microsoft App Installer package with a trusted Microsoft Corporation signature. This change has not been shown to explain the reported detection. Please determine whether the submitted sample is incorrectly classified.

For the latest sample, additionally include:

> The user reports a repeated virus block after downloading the scrollbar drag-fix release from commit bb6d567f787e66038c0c9dbb618be968ac9d9526. The exact published EXE is 185344 bytes, SHA-256 71D00B8D9A191A90E547DBE89B808926260CBD9FBCAE7588370E827798004FBC. The latest alert's detection name and affected-file hash are unconfirmed. This sample again scans clean locally with intelligence 1.461.27.0 and engine 1.1.26090.9. The README now warns against using the prebuilt downloads pending analysis.

Keep protection enabled and use the recommended quarantine/remediation action for the flagged file while its identity and classification remain unresolved. Do not add an exclusion to run it.
