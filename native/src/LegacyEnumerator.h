#pragma once
#include "MenuModel.h"
#include <string>
#include <vector>

namespace legacy {

// Builds the real merged classic menu for the given paths and returns its
// item tree. Called by the worker process, so a crashing legacy handler can
// never take Explorer down.
//
// When hideShellBuiltins is true, the shell's own duplicate verbs (Copy, Cut,
// Delete, Properties, Pin to Start, ...) are dropped from the TOP level only;
// submenus (including "Send to" and app submenus) are always preserved intact.
HRESULT BuildSnapshot(const std::vector<std::wstring>& paths,
                      bool hideShellBuiltins,
                      std::vector<LegacyMenuItem>& outItems);

// Rebuilds the classic menu and invokes the matching item.
//  - If `verb` is non-empty it is invoked directly by its canonical verb.
//  - Otherwise the menu is matched by offset, then by title, and invoked by id.
// Returns HRESULT_FROM_WIN32(ERROR_NOT_FOUND) when the menu changed order.
HRESULT Invoke(const std::vector<std::wstring>& paths,
               UINT offset,
               const std::wstring& verb,
               const std::wstring& title);

} // namespace legacy
