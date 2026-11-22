#include "Common.h"
#include "LegacyEnumerator.h"
#include "Guid.h"
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

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

static std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                        &out[0], n, nullptr, nullptr);
    return out;
}

static std::string HexA(HRESULT hr) {
    wchar_t wb[16] = { 0 };
    swprintf_s(wb, L"%08X", (unsigned)hr);
    return WideToUtf8(wb);
}

static const DWORD kMaxFrame = 32u * 1024u * 1024u;

static bool ReadAll(HANDLE h, void* buf, DWORD n) {
    char* p = static_cast<char*>(buf);
    DWORD done = 0;
    while (done < n) {
        DWORD read = 0;
        if (!ReadFile(h, p + done, n - done, &read, nullptr) || read == 0)
            return false;
        done += read;
    }
    return true;
}

static bool WriteAll(HANDLE h, const void* buf, DWORD n) {
    const char* p = static_cast<const char*>(buf);
    DWORD done = 0;
    while (done < n) {
        DWORD written = 0;
        if (!WriteFile(h, p + done, n - done, &written, nullptr) || written == 0)
            return false;
        done += written;
    }
    return true;
}

static bool ReadFrame(HANDLE h, std::string& payload) {
    DWORD len = 0;
    if (!ReadAll(h, &len, sizeof(len))) return false;
    if (len > kMaxFrame) return false;
    payload.assign((size_t)len, '\0');
    if (len && !ReadAll(h, &payload[0], len)) return false;
    return true;
}

static bool WriteFrame(HANDLE h, const std::string& payload) {
    DWORD len = (DWORD)payload.size();
    if (!WriteAll(h, &len, sizeof(len))) return false;
    if (len && !WriteAll(h, payload.data(), len)) return false;
    FlushFileBuffers(h);
    return true;
}

static void SerializeTree(const std::vector<LegacyMenuItem>& items, int depth,
                          std::string& out) {
    for (const auto& it : items) {
        out += std::to_string(depth);  out.push_back('\x1F');
        out += std::to_string(it.offset); out.push_back('\x1F');
        out += WideToUtf8(it.verb);    out.push_back('\x1F');
        out += WideToUtf8(it.icon);    out.push_back('\x1F');
        out += WideToUtf8(it.title);   out.push_back('\n');
        SerializeTree(it.children, depth + 1, out);
    }
}

static void DumpTree(const std::vector<LegacyMenuItem>& items, int depth) {
    for (const auto& it : items) {
        std::wstring indent(depth > 0 ? (size_t)depth * 2 : 0, L' ');
        std::wstring line = indent + it.title;
        line += L"  [offset=" + std::to_wstring(it.offset);
        if (!it.verb.empty()) line += L", verb=" + it.verb;
        if (!it.icon.empty()) line += L", icon=" + it.icon;
        line += L"]";
        PrintLine(line);
        DumpTree(it.children, depth + 1);
    }
}

static std::vector<std::string> SplitRequest(const std::string& req) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t sep = req.find('\x1F', start);
        if (sep == std::string::npos) { parts.push_back(req.substr(start)); break; }
        parts.push_back(req.substr(start, sep - start));
        start = sep + 1;
    }
    return parts;
}

static std::string HandleRequest(const std::string& req) {
    const std::vector<std::string> parts = SplitRequest(req);
    if (parts.empty()) return "ERR bad";

    if (parts[0] == "ENUM") {
        if (parts.size() < 3) return "ERR bad";
        const bool hide = (parts[1] == "1");
        std::vector<std::wstring> paths;
        for (size_t i = 2; i < parts.size(); ++i) paths.push_back(Utf8ToWide(parts[i]));
        std::vector<LegacyMenuItem> items;
        const HRESULT hr = BuildSnapshot(paths, hide, items);
        if (FAILED(hr)) return "ERR " + HexA(hr);
        std::string out = "OK\n";
        SerializeTree(items, 0, out);
        return out;
    }

    if (parts[0] == "INVOKE") {
        if (parts.size() < 5) return "ERR bad";
        const UINT offset = (UINT)wcstoul(Utf8ToWide(parts[1]).c_str(), nullptr, 10);
        const std::wstring verb = Utf8ToWide(parts[2]);
        const std::wstring title = Utf8ToWide(parts[3]);
        std::vector<std::wstring> paths;
        for (size_t i = 4; i < parts.size(); ++i) paths.push_back(Utf8ToWide(parts[i]));
        const HRESULT hr = Invoke(paths, offset, verb, title);
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) return "NOTFOUND";
        if (FAILED(hr)) return "ERR " + HexA(hr);
        return "OK";
    }

    return "ERR unknown";
}

static int RunServer(bool oneShot) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!in || in == INVALID_HANDLE_VALUE || !out || out == INVALID_HANDLE_VALUE)
        return 1;

    for (;;) {
        std::string req;
        if (!ReadFrame(in, req)) break;
        const std::string resp = HandleRequest(req);
        if (!WriteFrame(out, resp)) break;
        if (oneShot) break;
    }
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    SetLogComponent(L"worker");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int rc = 0;

    if (argc >= 2 && wcscmp(argv[1], L"--serve") == 0) {
        rc = RunServer(false);

    } else if (argc >= 2 && wcscmp(argv[1], L"--oneshot") == 0) {
        rc = RunServer(true);

    } else if (argc >= 2 && wcscmp(argv[1], L"--selftest") == 0) {
        wchar_t sys[MAX_PATH] = { 0 };
        GetSystemDirectoryW(sys, MAX_PATH);
        std::vector<std::wstring> paths;
        paths.push_back(std::wstring(sys) + L"\\notepad.exe");

        std::vector<LegacyMenuItem> items;
        HRESULT hr = BuildSnapshot(paths, false, items);
        PrintLine(L"selftest hr=0x" + Hex(hr) + L" items=" + std::to_wstring(items.size()));
        DumpTree(items, 0);
        rc = (SUCCEEDED(hr) && !items.empty()) ? 0 : 1;

    } else if (argc >= 2 && wcscmp(argv[1], L"--dump") == 0) {
        bool hide = true;
        std::vector<std::wstring> paths;
        for (int i = 2; i < argc; ++i) {
            if (wcscmp(argv[i], L"--show-shell") == 0) hide = false;
            else paths.push_back(argv[i]);
        }
        std::vector<LegacyMenuItem> items;
        HRESULT hr = BuildSnapshot(paths, hide, items);
        PrintLine(L"dump hr=0x" + Hex(hr));
        if (SUCCEEDED(hr)) DumpTree(items, 0);
        rc = SUCCEEDED(hr) ? 0 : 1;

    } else if (argc >= 2 && wcscmp(argv[1], L"--enumerate") == 0) {
        bool hide = true;
        std::vector<std::wstring> paths;
        for (int i = 2; i < argc; ++i) {
            if (wcscmp(argv[i], L"--show-shell") == 0) hide = false;
            else paths.push_back(argv[i]);
        }
        std::vector<LegacyMenuItem> items;
        HRESULT hr = BuildSnapshot(paths, hide, items);
        if (FAILED(hr)) {
            PrintLine(L"HR " + Hex(hr));
            rc = 1;
        } else {
            PrintLine(L"OK " + std::to_wstring(items.size()));
            DumpTree(items, 0);
        }

    } else if (argc >= 6 && wcscmp(argv[1], L"--invoke") == 0) {
        UINT offset = (UINT)wcstoul(argv[2], nullptr, 10);
        std::wstring verb = argv[3];
        std::wstring title = argv[4];
        std::vector<std::wstring> paths(argv + 5, argv + argc);
        HRESULT hr = Invoke(paths, offset, verb, title);
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) PrintLine(L"NOTFOUND");
        else if (FAILED(hr)) { PrintLine(L"HR " + Hex(hr)); rc = 1; }
        else PrintLine(L"OK");

    } else if (argc >= 2 && wcscmp(argv[1], L"--comtest") == 0) {
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (slash) *slash = 0;
        std::wstring dll = std::wstring(exePath) + L"\\LegacyMenuBridge.dll";

        HMODULE mod = LoadLibraryW(dll.c_str());
        if (!mod) {
            PrintLine(L"COMTEST FAIL: LoadLibrary " + dll);
            rc = 1;
        } else {
            using DllGetClassObjectFn = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
            auto pfn = (DllGetClassObjectFn)GetProcAddress(mod, "DllGetClassObject");
            if (!pfn) {
                PrintLine(L"COMTEST FAIL: DllGetClassObject export missing");
                rc = 1;
            } else {
                Microsoft::WRL::ComPtr<IClassFactory> factory;
                HRESULT hr = pfn(CLSID_LegacyMenuCommand, IID_IClassFactory, (void**)factory.GetAddressOf());
                if (FAILED(hr)) {
                    PrintLine(L"COMTEST FAIL: DllGetClassObject hr=0x" + Hex(hr));
                    rc = 1;
                } else {
                    Microsoft::WRL::ComPtr<IExplorerCommand> cmd;
                    hr = factory->CreateInstance(nullptr, IID_IExplorerCommand, (void**)cmd.GetAddressOf());
                    if (FAILED(hr) || !cmd) {
                        PrintLine(L"COMTEST FAIL: CreateInstance hr=0x" + Hex(hr));
                        rc = 1;
                    } else {
                        LPWSTR title = nullptr;
                        HRESULT ht = cmd->GetTitle(nullptr, &title);
                        LPWSTR icon = nullptr;
                        HRESULT hi = cmd->GetIcon(nullptr, &icon);
                        EXPCMDFLAGS flags = ECF_DEFAULT;
                        HRESULT hf = cmd->GetFlags(&flags);
                        GUID canon = { 0 };
                        HRESULT hc = cmd->GetCanonicalName(&canon);
                        LPWSTR tip = nullptr;
                        HRESULT htip = cmd->GetToolTip(nullptr, &tip);
                        EXPCMDSTATE state = ECS_HIDDEN;
                        HRESULT hstat = cmd->GetState(nullptr, TRUE, &state);

                        bool ok = (ht == S_OK && title && wcscmp(title, L"Classic Menu") == 0)
                               && (hi == S_OK && icon && wcslen(icon) > 0)
                               && (hf == S_OK && (flags & ECF_HASSUBCOMMANDS) != 0)
                               && (hc == S_OK)
                               && (htip == E_NOTIMPL)
                               && (hstat == S_OK && state == ECS_ENABLED);

                        PrintLine(L"COMTEST GetTitle hr=0x" + Hex(ht) + L" title=" + std::wstring(title ? title : L"(null)"));
                        PrintLine(L"COMTEST GetIcon hr=0x" + Hex(hi) + L" icon=" + std::wstring(icon ? icon : L"(null)"));
                        PrintLine(L"COMTEST GetFlags hr=0x" + Hex(hf));
                        PrintLine(L"COMTEST GetCanonicalName hr=0x" + Hex(hc));
                        PrintLine(L"COMTEST GetToolTip hr=0x" + Hex(htip));
                        PrintLine(L"COMTEST GetState hr=0x" + Hex(hstat));
                        PrintLine(ok ? L"COMTEST PASS" : L"COMTEST FAIL");
                        rc = ok ? 0 : 1;

                        if (title) CoTaskMemFree(title);
                        if (icon) CoTaskMemFree(icon);
                        if (tip) CoTaskMemFree(tip);
                    }
                }
            }
            FreeLibrary(mod);
        }
    } else {
        PrintLine(L"LegacyMenuBridge worker (internal).");
        PrintLine(L"--serve | --oneshot | --selftest | --dump <path>... [--show-shell]");
        PrintLine(L"--enumerate [--show-shell] <path>... | --invoke <offset> <verb> <title> <path>...");
    }

    CoUninitialize();
    return rc;
}
