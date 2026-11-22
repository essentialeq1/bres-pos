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

The first use is slow (~1-2s) because legacy handler DLLs are cold. The worker
process then stays resident (`--serve`), keeping those handler DLLs warm, and
the snapshot is cached per selection — so later menus open almost instantly and
the other modern extensions are no longer held up.

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

By default the shell's own duplicate verbs (Open, Copy, Cut, Delete, Rename,
Properties, Pin to…, Create shortcut, …) are hidden at the top level so the
flyout focuses on app options. Set the environment variable
`LEGACYBRIDGE_SHOW_SHELL=1` (then restart Explorer) to show them again.

## Known limitations (milestone 1)

- Items live under one modern-menu entry (`Classic Menu`) because Windows groups
  a multi-verb extension into a flyout; the items themselves are no longer
  behind *Show more options*.
- Handlers that only create owner-draw (`MFT_OWNERDRAW`) items whose text is not
  available via `GetCommandString` may be listed with a fallback verb name or
  skipped.
- The per-item icon is only as good as the handler's `GCS_VERBICONW` answer;
  items without one fall back to the module icon.
- Registered for files (`*`) and folders (`Directory`). Drive/background can be
  added in the manifest with `desktop10:ItemType Type="Drive"` /
  `"DesktopBackground"` (Win11 22H2+).
- First open of a new selection is ~1-2s while legacy handler DLLs load; later
  opens reuse the warm worker and per-selection cache.

## Notes

- The enumeration strategy is modelled on Nilesoft Shell (MIT). We do not copy
  its menu renderer; we only re-use the "build the real classic menu and read
  it" approach, which the Win32 shell already exposes through `IContextMenu`.
- The process must stay resident between enumeration and invoke to keep legacy
  handler DLLs warm; the COM server is registered with a surrogate and
  `DllCanUnloadNow` returns `S_FALSE`.
