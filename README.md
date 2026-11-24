# bres-pos — LegacyMenuBridge

Brings the **entire classic (legacy) Windows context menu** into the **Windows 11
modern context menu** — **live**, for the exact item you right-click — by running
the real classic menu in a background worker. No registry scanning, no JSON
imports, nothing is committed to disk.

## Active implementation

See [`native/`](native/README.md) — a native C++ `IExplorerCommand` shell
extension (thin DLL hosted in a COM surrogate) plus a `LegacyMenuBridge.exe`
worker that builds and re-invokes the real legacy menu **out-of-process**, so a
crashing third-party handler can never take Explorer down.

- **Download:** https://github.com/essentialeq1/bres-pos/releases/latest
- **Install:** unzip, then double-click `install.bat` (Developer Mode required),
  or run `powershell -ExecutionPolicy Bypass -File .\register.ps1`
- **Remove:** double-click `uninstall.bat`

## How it works (short version)

1. Windows 11 hands the extension the selected item(s).
2. The thin DLL talks to a long-lived worker that silently builds the **actual
   classic menu** for those item(s) (`IShellFolder::GetUIObjectOf(IContextMenu)`
   + `QueryContextMenu`), reads the nested submenus, verbs and icons, and returns
   them to the modern menu.
3. Picking an item tells the worker to invoke it by canonical verb (or command
   id). Snapshots are cached per file type, so repeat menus open instantly.

Logs: `%TEMP%\LegacyMenuBridgeNative-*.log`

## Archived prototype

`legacy-csharp/` contains the earlier C# prototype that **scanned the registry
and imported JSON into IKAS-MC's Custom Context Menu**. It is superseded and kept
for reference only.
