#include "Common.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace legacy {

namespace {
SRWLOCK g_lock = SRWLOCK_INIT;
std::wstring g_logFile;
std::wstring g_component = L"app";
bool g_initDone = false;

void EnsureInit() {
    if (g_initDone) return;
    g_initDone = true;

    // %TEMP% is reliable across every process (Explorer, the dllhost surrogate,
    // and the worker), unlike LOCALAPPDATA which can be virtualized for the
    // packaged COM surrogate.
    wchar_t temp[MAX_PATH] = { 0 };
    if (GetTempPathW(MAX_PATH, temp) == 0) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t name[MAX_PATH];
    swprintf_s(name, L"%sLegacyMenuBridgeNative-%04u%02u%02u-%02u%02u%02u.log",
               temp, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_logFile = name;
}
} // namespace

void SetLogComponent(const wchar_t* name) {
    if (!name) return;
    AcquireSRWLockExclusive(&g_lock);
    g_component = name;
    ReleaseSRWLockExclusive(&g_lock);
}

void Log(const wchar_t* fmt, ...) {
    if (!fmt) return;

    wchar_t body[2048];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t line[2320];
    _snwprintf_s(line, _countof(line), _TRUNCATE,
        L"[%04u-%02u-%02u %02u:%02u:%02u][%s] %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        g_component.c_str(), body);

    OutputDebugStringW(line);

    AcquireSRWLockExclusive(&g_lock);
    EnsureInit();
    if (!g_logFile.empty()) {
        HANDLE h = CreateFileW(g_logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                std::string utf8(bytes - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, line, -1, &utf8[0], bytes, nullptr, nullptr);
                DWORD written = 0;
                WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
            }
            CloseHandle(h);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace legacy
