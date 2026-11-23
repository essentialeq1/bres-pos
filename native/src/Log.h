#pragma once

namespace legacy {

// Sets a tag written on every line (e.g. "dll" or "exe").
void SetLogComponent(const wchar_t* name);

// Thread-safe printf-style logger. Writes to OutputDebugString and to
// %TEMP%\LegacyMenuBridgeNative.log.
void Log(const wchar_t* fmt, ...);

// True when timing instrumentation is requested via the LEGACYBRIDGE_TIMING
// environment variable (any value other than "0" enables it). Timing logs are
// opt-in so normal operation stays quiet.
bool TimingEnabled();

// Logs a line only when timing is enabled (no-op otherwise).
void TimingLog(const wchar_t* fmt, ...);

// RAII scope timer for the timing log. Logs "<name>: begin" on construction and
// "<name>: <N> ms" on destruction (or on Stop()) — both only when timing is
// enabled. Use string literals for `name` (the pointer is retained, not copied).
class ScopedTimer {
public:
    explicit ScopedTimer(const wchar_t* name);
    ~ScopedTimer();
    void Stop();
private:
    const wchar_t* _name;
    unsigned long long _start;
    bool _active;
};

} // namespace legacy
