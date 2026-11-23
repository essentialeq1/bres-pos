# LegacyMenuBridge (native)

Brings the **entire classic (legacy) context menu** into the **Windows 11 modern
context menu**, using the same mechanism apps like NanaZip/7-Zip use to appear in
the modern menu, and the same mechanism Nilesoft Shell uses to read the legacy
menu.

This `native/` tree replaces the earlier C#/IKAS-MC prototype. That prototype
depended on a third-party app (Custom Context Menu) and flattened the registry
into JSON; this version is a real shell extension.

## How it works

> **Note (v0.1.8+):** the heavy/enumeration work now runs in a **separate worker
> process** (`LegacyMenuBridge.exe`). The shell-extension DLL (`LegacyMenuBridge.dll`, hosted in a COM surrogate) is a
> thin shim that only spawns that worker, so a crashing legacy handler can never take
> Explorer down.


1. Windows 11 opens the modern menu and hands our `IExplorerCommand` the
   selection (`IShellItemArray`) via `IObjectWithSelection::SetSelection`.
2. `EnumSubCommands()` **silently builds the real classic menu** for that exact
   selection: it binds to the parent `IShellFolder`, calls
   `GetUIObjectOf(IID_IContextMenu)`, then `QueryContextMenu` into a hidden
   `HMENU`. That single object already contains the static verbs **and** every
   registered `ContextMenuHandlers` entry - i.e. the old menu in its entirety.
3. We walk the hidden menu (recursing into submenus and forwarding
   `WM_INITMENUPOPUP` so lazy submenus like *Send to* are populated), keeping
   the **tree structure**, each item's canonical **verb** and **icon**
   location, and return the result to the modern menu.
4. When the user picks an item, `Invoke()` re-runs the worker and invokes the
   chosen item **by its canonical verb** when available (which is what makes
   Copy/Cut/Delete/Properties/Pin to Start work), otherwise by command id
   (matched by offset, then by title).

Menu enumeration never waits on that build: the snapshot is built on a
background thread (started as soon as the selection is known) and cached per
selection, so the Windows 11 menu — and every other modern extension — appears
immediately. If a build is still in flight, the flyout shows a disabled
"Loading..." entry until the next open.

## Files

| File | Purpose |
|------|---------|
| `src/Common.h` | Common Win32/SDK includes |
| `src/Guid.h` | CLSID of the command provider |
| `src/MenuModel.h` | `LegacyMenuItem` tree (title / verb / icon / children) |
| `src/Log.h/.cpp` | File + debug logger (`%TEMP%\LegacyMenuBridgeNative-*.log`) |
| `src/LegacyEnumerator.h/.cpp` | Build/snapshot/re-invoke the classic menu (the core) |
| `src/Snapshot.h/.cpp` | Persistent worker + framing protocol + per-selection cache |
| `src/ExplorerCommand.h/.cpp` | `IExplorerCommand` + `IEnumExplorerCommand` + `IObjectWithSelection` + `IShellExtInit` |
| `src/EnumExplorerCommand.cpp` | Sub-command enumerator |
| `src/dllmain.cpp` | `DllGetClassObject`, class factory, exports |
| `src/main.cpp` | Worker EXE (`--serve` loop, `--oneshot`, `--dump`, `--selftest`, `--comtest`) |
| `src/LegacyMenuBridge.rc.in` | Embedded module icon (generated to `.rc`) |
| `src/exports.def` | COM exports |
| `packaging/AppxManifest.xml` | Sparse package: COM server + `windows.fileExplorerContextMenus` |
| `packaging/*.ps1` | build / create-cert / register / unregister |

## Build

Requirements: Windows 10/11, Visual Studio 2022 (C++ workload), CMake 3.20+.

```powershell
cd native\packaging
.\build.ps1
```

This produces `native\build\x64\Release\LegacyMenuBridge.dll` and
`LegacyMenuBridge.exe`, and copies `AppxManifest.xml` next to them.

## Install (sideload)

Two supported paths:

**A. Developer Mode (fastest, no signing).** Enable *Settings > Privacy &
Security > For developers > Developer Mode*, then:

```powershell
cd native\packaging
.\register.ps1
```

**B. Self-signed package.** Create and trust a certificate whose Subject matches
the manifest `Publisher` (`CN=LegacyMenuBridge`), then register:

```powershell
cd native\packaging
.\create-cert.ps1
.\register.ps1
```

To undo: `.\unregister.ps1`.

If the entry does not show up, restart Explorer (Task Manager > Windows
Explorer > Restart) or sign out and back in.

## Using it

Right-click a file or folder in Explorer and choose **Classic Menu**. The flyout
contains the legacy items as **nested submenus** (no more flattening), with
per-item icons when the handler provides them. Picking any item runs it through
the real legacy handler.

By default the shell's own duplicate entries (Open, Open with, Send to, Give
access to, Copy as path, Copy, Cut, Delete, Rename, Properties, Pin to…, Create
shortcut, Include in library, Troubleshoot compatibility, …) are hidden at the
top level so the flyout focuses on app options. App-owned submenus (7-Zip,
Kaspersky, …) and everything inside them are preserved. Set the environment
variable `LEGACYBRIDGE_SHOW_SHELL=1` (then restart Explorer) to show them all
again.

## Known limitations (milestone 1)

- Items live under one modern-menu entry (`Classic Menu`) because Windows groups
  a multi-verb extension into a flyout; the items themselves are no longer
  behind *Show more options*.
- Handlers that only create owner-draw (`MFT_OWNERDRAW`) items whose text is not
  available via `GetCommandString` may be listed with a fallback verb name or
  skipped.
- Per-item icons come first from `GCS_VERBICONW` (normalized to an absolute,
  quoted `"path",index`), then from the verb's registry `Icon` value, then — for
  elevated `runas`/`runasuser` verbs — the UAC shield (`imageres.dll,-78`).
  Items with none of those fall back to the module icon.
- Registered for files (`*`) and folders (`Directory`). Drive/background can be
  added in the manifest with `desktop10:ItemType Type="Drive"` /
  `"DesktopBackground"` (Win11 22H2+).
- A brand-new selection whose snapshot was never cached may show a disabled
  "Loading..." entry on its first open while the background worker builds the
  menu; the next open reuses the warm worker and cache and is instant.

## Notes

- The enumeration strategy is modelled on Nilesoft Shell (MIT). We do not copy
  its menu renderer; we only re-use the "build the real classic menu and read
  it" approach, which the Win32 shell already exposes through `IContextMenu`.
- The process must stay resident between enumeration and invoke to keep legacy
  handler DLLs warm; the COM server is registered with a surrogate and
  `DllCanUnloadNow` returns `S_FALSE`.
