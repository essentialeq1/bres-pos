#pragma once
#include <windows.h>
#include <string>

namespace legacy {

// Converts `hbmp` (a real, non-sentinel menu bitmap) into a cached .ico file
// under %LOCALAPPDATA%\bres-pos\icons, with 16 px and 32 px frames and alpha
// preserved. `key` is a stable string (file type + verb + title) so identical
// items reuse the same file and an unchanged file is never rewritten. Returns
// the absolute icon path (quoted when it contains spaces), or an empty string on
// failure.
std::wstring CacheBitmapIcon(HBITMAP hbmp, const std::wstring& key);

} // namespace legacy