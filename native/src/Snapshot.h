#pragma once
#include "MenuModel.h"
#include <string>
#include <vector>

namespace legacy {

// Returns the legacy menu tree for the given selection paths.
// Results are cached per selection, so repeat opens are instant.
// Returns an empty tree when the menu could not be built.
std::vector<LegacyMenuItem> GetSnapshot(const std::vector<std::wstring>& paths);

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
