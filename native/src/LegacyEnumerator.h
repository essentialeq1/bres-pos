#pragma once
#include "MenuModel.h"
#include <shobjidl_core.h>
#include <vector>

namespace legacy {

// Builds the real, merged legacy context menu for the given selection and
// returns a flattened list of its actionable items. This is the same object
// Explorer's classic ("Show more options") menu is built from, so it already
// contains the static verbs plus every registered ContextMenuHandlers entry.
HRESULT BuildLegacyMenuSnapshot(IShellItemArray* selection,
                                std::vector<FlatLegacyItem>& outItems);

// Rebuilds the legacy menu for the selection and invokes the item that
// corresponds to (offset, title). Returns HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
// when the menu no longer contains a matching item (order changed).
HRESULT InvokeLegacyItem(IShellItemArray* selection,
                         UINT offset,
                         const std::wstring& title);

} // namespace legacy
