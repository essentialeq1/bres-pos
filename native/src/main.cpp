#include "Common.h"
#include "LegacyEnumerator.h"
#include "Log.h"

#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

using namespace legacy;

static void PrintLine(const std::wstring& line) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
                                    nullptr, 0, nullptr, nullptr);
    std::string utf8;
    if (bytes > 0) {
        utf8.resize(bytes);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
                            &utf8[0], bytes, nullptr, nullptr);
    }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (h && h != INVALID_HANDLE_VALUE) {
        WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        const char nls[2] = { '\r', '\n' };
        WriteFile(h, nls, 2, &written, nullptr);
    }
}

static std::wstring Hex(HRESULT hr) {
    wchar_t buf[16] = { 0 };
    swprintf_s(buf, L"%08X", (unsigned)hr);
    return std::wstring(buf);
}

int wmain(int argc, wchar_t** argv) {
    SetLogComponent(L"worker");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int rc = 0;

    if (argc >= 2 && wcscmp(argv[1], L"--selftest") == 0) {
        wchar_t sys[MAX_PATH] = { 0 };
        GetSystemDirectoryW(sys, MAX_PATH);
        std::vector<std::wstring> paths;
        paths.push_back(std::wstring(sys) + L"\\notepad.exe");

        std::vector<FlatLegacyItem> items;
        HRESULT hr = BuildSnapshot(paths, items);
        PrintLine(L"selftest hr=0x" + Hex(hr) + L" items=" + std::to_wstring(items.size()));
        for (const auto& i : items) PrintLine(std::to_wstring(i.offset) + L"\t" + i.title);
        rc = (SUCCEEDED(hr) && !items.empty()) ? 0 : 1;

    } else if (argc >= 2 && wcscmp(argv[1], L"--enumerate") == 0) {
        std::vector<std::wstring> paths(argv + 2, argv + argc);
        std::vector<FlatLegacyItem> items;
        HRESULT hr = BuildSnapshot(paths, items);
        if (FAILED(hr)) {
            PrintLine(L"HR " + Hex(hr));
            rc = 1;
        } else {
            PrintLine(L"OK " + std::to_wstring(items.size()));
            for (const auto& i : items) PrintLine(std::to_wstring(i.offset) + L"\t" + i.title);
        }

    } else if (argc >= 5 && wcscmp(argv[1], L"--invoke") == 0) {
        UINT offset = (UINT)wcstoul(argv[2], nullptr, 10);
        std::wstring title = argv[3];
        std::vector<std::wstring> paths(argv + 4, argv + argc);
        HRESULT hr = Invoke(paths, offset, title);
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) PrintLine(L"NOTFOUND");
        else if (FAILED(hr)) { PrintLine(L"HR " + Hex(hr)); rc = 1; }
        else PrintLine(L"OK");

    } else {
        PrintLine(L"LegacyMenuBridge worker (internal).");
        PrintLine(L"--selftest | --enumerate <path>... | --invoke <offset> <title> <path>...");
    }

    CoUninitialize();
    return rc;
}
