# Diagnostics & baseline timings

This file records the **Phase 0 diagnostic baseline** and how to reproduce it.
It is intentionally plain text so it can be filled in on a Windows machine.

## Enable timing logs

Set the environment variable `LEGACYBRIDGE_TIMING` to anything other than `0`
(e.g. `1`), then restart Explorer (or restart the worker / COM surrogate) so the
new setting is picked up:

```powershell
[Environment]::SetEnvironmentVariable("LEGACYBRIDGE_TIMING", "1", "User")
```

Timing lines are prefixed `[timing]` and written to the normal log
(`%TEMP%\LegacyMenuBridgeNative-*.log`) and `OutputDebugString`. They cover:

- DLL load (`DLL_PROCESS_ATTACH host=...`)
- request received (worker `ENUM` / `INVOKE`)
- worker start (`worker: start process`), one-shot/persistent transactions,
  `worker: BuildSnapshot` / `worker: Invoke`, async build
- time spent on the Explorer menu thread (`menu: GetState`,
  `menu: EnumSubCommands`, `menu: Initialize`, `menu: SetSelection`, `menu: Invoke`)
- cache hit / miss

## Baseline capture (Phase 0)

On a machine with **Adobe Acrobat** (or any handler that registers "Combine
files in Acrobat" / "Convert to Adobe PDF") **and** a "Take ownership" handler
installed, run:

```powershell
cd native\build\x64\Release
& .\LegacyMenuBridge.exe --dump "C:\some\file.pdf" --show-shell
```

Each line now also prints the icon provenance (`src=`):

| src      | meaning                                   |
|----------|-------------------------------------------|
| `verb`   | icon came from the verb's `GCS_VERBICONW` |
| `registry` | icon came from the verb's registry `Icon` value |
| `runas`  | UAC shield was synthesized (`imageres.dll,-78`) |
| `none`   | no icon resolved (falls back to module icon) |

Paste the dump output (or the lines for Acrobat's "Combine files in Acrobat" /
"Convert to Adobe PDF" and the "Take ownership" item) below as the baseline.

```text
(PASTE `--dump` OUTPUT HERE)
```

## Measured timings (before / after)

| Metric | Before (Phase 0) | After (Phase 3) |
|--------|------------------|-----------------|
| menu-thread GetState (ms) | | |
| menu-thread EnumSubCommands (ms) | | |
| worker BuildSnapshot (ms) | | |
| first right-click (cold, ms) | | |
| repeat right-click (warm, ms) | | |

> Note: this file was authored in a Linux environment without a Windows/MSVC
> toolchain, so the baseline above could not be captured here. Run it once on a
> Windows 10/11 machine (or in the `windows-latest` GitHub Actions workflow) and
> fill in the values.