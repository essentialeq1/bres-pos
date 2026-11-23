#pragma once
#include <string>
#include <vector>

namespace legacy {

// One node of the real classic (legacy) context menu, preserved as a tree so
// the modern menu can reproduce nested submenus instead of flattening them.
//
//  - Leaves (children.empty()) are actionable items.
//  - Nodes with children are submenus.
struct LegacyMenuItem {
    std::wstring title;   // display text
    std::wstring verb;    // canonical verb (GCS_VERBW) when the handler exposes one
    std::wstring icon;    // icon location: "path,index" (verb/registry) or ".ico" path (bitmap)
    std::wstring iconSource; // provenance of `icon`: "verb" | "registry" | "runas" | "bitmap" | "none"
    UINT         offset = 0; // command id relative to idCmdFirst (for id invocation)
    std::vector<LegacyMenuItem> children; // non-empty => submenu

    bool IsSubmenu() const { return !children.empty(); }
};

} // namespace legacy
