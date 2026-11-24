# Verification checklist

Run on **Windows 10/11** (or the `windows-latest` GitHub Actions runner) after a
successful build:

```powershell
cd native\packaging
.\build.ps1
cd ..\build\x64\Release
```

The Linux authoring environment has no MSVC/Wine, so these steps — and the
timing numbers in `DIAGNOSTICS.md` — must be run on Windows.

## Test matrix

| Selection | What to confirm |
|-----------|-----------------|
| A `.txt` file | No duplicate Open/Edit/Copy/Delete/Properties; classic-only Edit/Print remain visible |
| A `.pdf` with Adobe Acrobat | "Combine files…", "Convert to Adobe PDF" show **their own icon or none**, never the UAC shield |
| A folder | No duplicate Open/Share/Give access-to; folders show the folder icon |
| The desktop background | No crash; empty/no-op flyout is acceptable (background not registered by default) |
| Multi-selection (2+ files) | Menu builds from the exact selection; invoke targets all selected items |
| A file inside OneDrive | Items may be briefly stale (per-file handlers); they refresh in the background |
| A freshly installed app | Newly installed verb appears after it registers (within the cache TTL, or immediately on a new file type) |

## Regression checks

1. **No remaining duplicates** — at the top level of the flyout, none of Copy,
   Paste, Cut, Delete, Rename, Properties, Open, Open with, Copy as path,
   Send to, Troubleshoot compatibility, Share appear; classic-only actions
   (Print, Edit, Play, Preview, Rotate, Restore previous versions,
   Set as desktop background) do not disappear.
   `.\LegacyMenuBridge.exe --dump <path> --show-shell` shows what is filtered.
2. **Correct icons** — `--dump <file.pdf>` prints `src=` per item; Acrobat items
   resolve to `src=verb`/`src=registry`/`src=bitmap` (or `none`), never a shield.
3. **No shield on non-runas** — no non-elevated item resolves to the UAC shield.
4. **Explorer stability** — right-click 50+ times across the matrix above without
   a hang or crash; the worker process stays resident and the menu stays instant.
5. **Worker lifecycle** — `Stop-Process -Name LegacyMenuBridge -Force` then
   right-click again: the worker is respawned automatically, no crash.

## Icon cache

- Bitmaps land under `%LOCALAPPDATA%\bres-pos\icons`.
- Deleting that folder clears it; it is rebuilt on the next right-click.

## Timing (opt-in)

```powershell
[Environment]::SetEnvironmentVariable("LEGACYBRIDGE_TIMING", "1", "User")
```

restart Explorer, reproduce the menu, then read `%TEMP%\LegacyMenuBridgeNative-*.log`
and record `menu: GetState` / `menu: EnumSubCommands` (expect near 0 ms) and
`worker: BuildSnapshot`.