# LegacyMenuBridge

Bridge the **classic** Windows context-menu entries (the ones hidden behind
*Show more options*) into the modern Windows 11 menu, by generating menu files
for [IKAS-MC **Custom Context Menu for Windows 11**](https://github.com/ikas-mc/ContextMenuForWindows11).

LegacyMenuBridge does not re-implement the Windows 11 menu. It reads the old
entries, converts them to the JSON files Custom Context Menu already understands,
and drops them into its `custom_commands` folder. Everything after that (toggle,
reorder, rename, grouping) is handled by Custom Context Menu's own UI.

## How it works

1. **Scan** - reads the registry for legacy entries:
   - simple `shell\<verb>\command` entries under `*`, `Directory`,
     `Directory\Background`, `Drive` and per-extension keys;
   - COM handlers under `shellex\ContextMenuHandlers` (e.g. Kaspersky, 7-Zip).
   Both the 64-bit and 32-bit registry views are read.
2. **Convert** - each simple entry is turned into an IKAS-MC JSON file with the
   correct `exe`, `param` (with `{path}` placeholders), `icon` and file/folder
   matching flags.
3. **COM entries** - the classic menu for a sample item is built in a hidden
   child process and its real items, names and verbs are read. Each item becomes
   a JSON entry that calls this tool back with `--invoke`, which rebuilds the
   menu for the file you actually right-clicked and fires the matching item.
4. **Import** - JSON files are written to
   `%LocalAppData%\Packages\{appId}\LocalState\custom_commands`.
   Already-imported entries are remembered in a log and skipped on re-scan.

## Requirements

- Windows 11, build 22000 or newer (where the modern context menu exists).
- [Custom Context Menu (IKAS-MC)](https://github.com/ikas-mc/ContextMenuForWindows11)
  installed, and opened at least once so its `custom_commands` folder exists.
- No administrator rights needed: everything is written inside your own user
  profile.

## Getting the .exe

Each push builds two files on a Windows runner (see the **Actions** tab):

- `LegacyMenuBridge.exe`   - the main 64-bit app (the one you open).
- `LegacyMenuBridge32.exe` - a background helper for 32-bit COM handlers.

Keep both in the same folder (for example `C:\Tools\LegacyMenuBridge\`).
COM entries need `LegacyMenuBridge.exe` to stay on disk (it does not need to be
running); if you move it, re-run an import so the stored paths are rewritten.

### Building locally

```powershell
dotnet publish LegacyMenuBridge.csproj -c Release -r win-x64 --self-contained -o publish/x64
dotnet publish LegacyMenuBridge.csproj -c Release -r win-x86 -p:PlatformTarget=x86 -p:AssemblyName=LegacyMenuBridge32 --self-contained -o publish/x86
```

Requires the .NET 8 SDK (or newer). The output is a single self-contained .exe,
no .NET installation needed on the target PC.

## Using it

1. Run `LegacyMenuBridge.exe`.
2. Click **Scan registry**.
3. Expand a group, tick the entries you want, click **Import selected**.
4. Open **Custom Context Menu** and toggle its **cache off and on** to apply the
   changes (this one manual step is a limitation of Custom Context Menu).

## What a generated file looks like

```json
{
  "title": "Open With Notepad",
  "index": 0,
  "exe": "\"C:\\Windows\\notepad.exe\"",
  "param": "\"{path}\"",
  "icon": "\"C:\\Windows\\notepad.exe\",0",
  "iconDark": "",
  "acceptDirectoryFlag": 0,
  "acceptFileFlag": 3,
  "acceptExts": ".txt|.log|.md",
  "acceptFileRegex": "",
  "acceptMultipleFilesFlag": 1,
  "acceptMultipleFilesRuleFlag": 0,
  "pathDelimiter": "",
  "paramForMultipleFiles": "",
  "showWindowFlag": 0,
  "runAsFlag": 0,
  "workingDirectory": ""
}
```

Matching flags follow IKAS-MC's enums: `acceptFileFlag` (0 none, 1 ext-like,
2 regex, 3 ext-list, 4 all) and `acceptDirectoryFlag` (1 directory, 2 background,
4 desktop, 8 drive).

## Hidden command modes

The generated entries call the tool back with one of these:

- `--inspect "<path>"` - prints the classic menu items for a path as JSON.
- `--invoke "<verb-or-title>" "<path>"` - fires the matching classic menu item.
- `--verb "<verb>" "<path>"` - fires a plain shell verb (DelegateExecute entries).

## Known limitations

- COM items a handler only creates when a submenu is opened are not seen; this is
  the same limitation a registry-only scanner has.
- Owner-drawn items without text are skipped.
- One file per click for now; multi-file selection is not wired up yet.
- `--verb`-style entries registered on the folder background may not fire.
- The 32-bit helper is only used when `LegacyMenuBridge32.exe` sits next to the
  main exe. Most current handlers are 64-bit.
- The `{appId}` folder is detected by searching `%LocalAppData%\Packages` for a
  `LocalState\custom_commands` folder, which works for both the Store and GitHub
  builds of Custom Context Menu.

## Repository name

This repository is currently named `bres-pos`. The tool itself is
`LegacyMenuBridge`; renaming the repo or moving the files under a
`LegacyMenuBridge/` folder will not affect the build.
