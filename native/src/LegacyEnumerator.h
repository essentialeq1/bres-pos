#pragma once
#include "MenuModel.h"
#include <string>
#include <vector>

namespace legacy {

// Builds the real merged classic menu for the given paths and returns a
// flattened list of actionable items. Called by the worker process, so a
// crashing legacy handler can never take Explorer down.
HRESULT BuildSnapshot(const std::vector<std::wstring>& paths,
                      std::vector<FlatLegacyItem>& outItems);

// Rebuilds the classic menu and invokes the matching item.
// Returns HRESULT_FROM_WIN32(ERROR_NOT_FOUND) when the menu changed order.
HRESULT Invoke(const std::vector<std::wstring>& paths,
               UINT offset,
               const std::wstring& title);

} // namespace legacy
