# Changelog

## v0.21

### Phase 0 — Diagnostics
- Opt-in timing logs (`LEGACYBRIDGE_TIMING=1`): DLL load, request received,
  worker start/transact/build/invoke, Explorer menu-thread callbacks, and cache
  hit/miss.
- `--dump` / `--selftest` now annotate each item's icon provenance
  (`verb` / `registry` / `runas` / `bitmap` / `none`).

### Phase 1 — Filter leaks
- Titles are normalized before filtering (`&` mnemonic, tab suffix, trailing
  `...` / `…`, trim, case-insensitive).
- Added missing `copyaspath` / `sendto` verbs; narrowed the blocklist to true
  Windows 11 duplicates so classic-only actions (Print, Edit, Play, Preview,
  Rotate, Restore previous versions, …) stay visible.
- Blocklist moved to an editable `LegacyMenuBridge.blocklist.txt` (one verb or
  normalized title per line, `#` comments) shipped next to the DLL with a
  built-in fallback.

### Phase 2 — Icons
- Icon priority: item bitmap (`hbmpItem`) → verb icon string → registry `Icon`
  → UAC shield only for the literal `runas` verb. Items never fall back to the
  module icon, so an unresolved item shows no icon.
- Handler bitmaps are converted to 16 px + 32 px alpha `.ico` files cached under
  `%LOCALAPPDATA%\bres-pos\icons`, keyed by a hash of file type + verb + title
  (unchanged files are reused, never rewritten).
- Non-`runas` items no longer show the UAC shield (Acrobat "Combine files",
  "Take ownership"). Owner-draw/callback bitmaps are intentionally not used.

### Phase 3 — No delay
- Cache keyed by file type (extension / folder / drive / background);
  multi-selection is exact-path.
- Stale-while-revalidate with a 10-minute TTL (`LEGACYBRIDGE_CACHE_TTL_MS`).
- Cold selections show a transient `Loading...` item; the menu thread never
  blocks (the ~200 ms bounded wait is gone).
- Worker prewarmed when the DLL first activates; concurrent requests run on a
  separate one-shot worker so a slow handler can't queue behind the next click.
- Invoke still rebuilds against the real selection; a stale item shows a clear
  "not found" notice instead of crashing.

## v0.20 and earlier
See git history and `native/README.md`.