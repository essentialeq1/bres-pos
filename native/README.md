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
immediately. While the build is still in flight, `GetState` reports `E_PENDING`
so the menu shows its native "loading" affordance (no fake disabled item); once
ready (typically within a moment, from the cache), `EnumSubCommands` returns the
real items. `GetState` and `EnumSubCommands` never block the menu thread for
more than ~200 ms.

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
- Per-item icons resolve in this order: (1) the bitmap the handler attaches to
  the item (`MENUITEMINFO hbmpItem`), converted to a cached `.ico`; (2) the verb
  icon string (`GCS_VERBICONW`, normalized to an absolute quoted `"path",index`);
  (3) the verb's registry `Icon` value; (4) the UAC shield (`imageres.dll,-78`)
  only when the verb is literally `runas`. Items that resolve nothing show no
  icon. Owner-draw/callback bitmaps (`IContextMenu2/3` draw messages) are not
  used, because forwarding draw messages risks crashing a legacy handler.
- Registered for files (`*`) and folders (`Directory`). Drive/background can be
  added in the manifest with `desktop10:ItemType Type="Drive"` /
  `"DesktopBackground"` (Win11 22H2+).
- A brand-new selection whose snapshot was never cached reports the native
  loading state (`E_PENDING`) on its first open while the background worker
  builds the menu; once cached (warm worker + per-selection cache), later opens
  are instant. The menu thread is never blocked for more than ~200 ms, so other
  extensions load in parallel; only a hung legacy handler shows a transient
  `Loading...` item until the next open.

## Notes

- The enumeration strategy is modelled on Nilesoft Shell (MIT). We do not copy
  its menu renderer; we only re-use the "build the real classic menu and read
  it" approach, which the Win32 shell already exposes through `IContextMenu`.
- The process must stay resident between enumeration and invoke to keep legacy
  handler DLLs warm; the COM server is registered with a surrogate and
  `DllCanUnloadNow` returns `S_FALSE`.
- Resolved item bitmaps are cached as `.ico` files under
  `%LOCALAPPDATA%\bres-pos\icons` (named by a hash of file type + verb + title,
  so unchanged icons are reused and never rewritten). To clear the cache and
  force fresh icons, delete that folder — Explorer rebuilds it on demand.
