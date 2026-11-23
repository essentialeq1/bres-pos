#pragma once
#include "MenuModel.h"
#include <string>
#include <vector>

namespace legacy {

// Returns the legacy menu tree for the given selection paths.
// Results are cached per selection, so repeat opens are instant.
// Returns an empty tree when the menu could not be built.
std::vector<LegacyMenuItem> GetSnapshot(const std::vector<std::wstring>& paths);

// Starts a background build of the legacy menu for the given selection (no
// blocking). This is called as soon as the selection is known so that, by the
// time EnumSubCommands needs the items, they are usually already cached.
void StartSnapshotWarmup(const std::vector<std::wstring>& paths);

// Returns the legacy menu tree if it is available within waitMs (either from
// the cache or from a background build that is already running). Returns true
// and fills `out` when ready; returns false when the build is still in flight,
// in which case the caller should show a "Loading..." placeholder and retry on
// the next menu open.
bool TryGetSnapshot(const std::vector<std::wstring>& paths,
                    DWORD waitMs,
                    std::vector<LegacyMenuItem>& out);

// Non-blocking "is it ready yet?" probe used by GetState to decide between
// reporting E_PENDING (still building) and ECS_ENABLED without ever blocking
// the menu thread. Returns true when the snapshot for `paths` is already
// cached or the background build has already completed.
bool IsSnapshotReady(const std::vector<std::wstring>& paths);

// Invokes a legacy item. Prefers the canonical verb when it is non-empty,
// otherwise falls back to command-offset invocation inside the worker.
// Returns HRESULT_FROM_WIN32(ERROR_NOT_FOUND) when the menu changed order.
HRESULT InvokeLegacyItem(const std::vector<std::wstring>& paths,
                         UINT offset,
                         const std::wstring& verb,
                         const std::wstring& title);

// Stops the persistent worker (used at DLL unload).
void ShutdownSnapshotWorker();

} // namespace legacy
