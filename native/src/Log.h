#pragma once

namespace legacy {

// Sets a tag written on every line (e.g. "dll" or "exe").
void SetLogComponent(const wchar_t* name);

// Thread-safe printf-style logger. Writes to OutputDebugString and to
// %LOCALAPPDATA%\LegacyMenuBridgeNative\log.txt.
void Log(const wchar_t* fmt, ...);

} // namespace legacy
