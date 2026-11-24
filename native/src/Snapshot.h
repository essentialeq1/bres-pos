#pragma once
#include "MenuModel.h"
#include <string>
#include <vector>

namespace legacy {

// Spawns the warm worker process once, off-thread, so the first right-click
// doesn't pay the CreateProcess + legacy-handler-DLL load cost. Safe to call
// repeatedly (it is a no-op after the first time).
void StartSnapshotWorkerPrewarm();

// Starts a background build for the given selection (no blocking), unless a
// fresh cache entry (within the TTL) already exists. Called as soon as the
// selection is known so the menu is warm by the next open.
void StartSnapshotWarmup(const std::vector<std::wstring>& paths);

// Returns the legacy menu tree immediately when a (possibly stale) cache entry
// exists for the selection's file type, scheduling a background refresh when it
// is expired (stale-while-revalidate). Returns false when there is no entry, in
// which case the caller shows a "Loading..." placeholder and retries on the next
// menu open. Never blocks the menu thread.
bool TryGetSnapshot(const std::vector<std::wstring>& paths,
                    std::vector<LegacyMenuItem>& out);

// Non-blocking "is it ready yet?" probe used by GetState to decide between
// E_PENDING (cold first open) and ECS_ENABLED without ever blocking the menu
// thread. Returns true when any cache entry exists for the selection.
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
