#pragma once
#include <string>

namespace legacy {

// One flattened option taken from the legacy (classic) context menu.
struct FlatLegacyItem {
    std::wstring title;      // display title, already prefixed with submenu context
    UINT         offset = 0; // command id relative to idCmdFirst (stable on rebuild)
    std::wstring iconPath;   // optional "file,index"; empty when unknown
};

} // namespace legacy
