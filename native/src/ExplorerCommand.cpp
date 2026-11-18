#include "Common.h"
#include "ExplorerCommand.h"
#include "Guid.h"
#include "Log.h"
#include "MenuModel.h"
#include "Module.h"

#include <cwchar>
#include <cstdlib>
#include <new>

namespace legacy {
namespace {

HRESULT DuplicateString(const std::wstring& value, LPWSTR* out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (value.empty()) return S_OK;
    const size_t cch = value.size();
    auto* buf = static_cast<LPWSTR>(CoTaskMemAlloc((cch + 1) * sizeof(wchar_t)));
    if (!buf) return E_OUTOFMEMORY;
    wmemcpy_s(buf, cch + 1, value.c_str(), cch);
    buf[cch] = L'\0';
    *out = buf;
    return S_OK;
}

HRESULT ExtractPaths(IShellItemArray* selection, std::vector<std::wstring>& paths) {
    if (!selection) return E_INVALIDARG;
    DWORD count = 0;
    HRESULT hr = selection->GetCount(&count);
    if (FAILED(hr)) return hr;
    for (DWORD i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (FAILED(selection->GetItemAt(i, item.GetAddressOf())) || !item) continue;
        PWSTR name = nullptr;
        hr = item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &name);
        if (SUCCEEDED(hr) && name) {
            paths.emplace_back(name);
            CoTaskMemFree(name);
        }
    }
    return paths.empty() ? E_FAIL : S_OK;
}

std::wstring QuoteArg(const std::wstring& a) {
    std::wstring q = L"\"";
    for (wchar_t c : a) {
        if (c == L'"') q += L"\\\"";
        else q += c;
    }
    q += L"\"";
    return q;
}

std::wstring WorkerExePath() {
    wchar_t dir[MAX_PATH] = { 0 };
    if (g_hInst) {
        GetModuleFileNameW(g_hInst, dir, MAX_PATH);
        PathRemoveFileSpecW(dir);
    } else {
        GetCurrentDirectoryW(MAX_PATH, dir);
    }
    return std::wstring(dir) + L"\\LegacyMenuBridge.exe";
}

bool LaunchWorker(const std::vector<std::wstring>& args, std::wstring& output) {
    output.clear();
    std::wstring exe = WorkerExePath();
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"worker exe missing: %s", exe.c_str());
        return false;
    }

    std::wstring cmd = L"\"" + exe + L"\"";
    for (const auto& a : args) cmd += L" " + QuoteArg(a);

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = { 0 };
    BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        Log(L"CreateProcess failed err=%lu", (unsigned long)GetLastError());
        return false;
    }

    std::string bytes;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0)
        bytes.append(buf, n);
    CloseHandle(hRead);

    if (WaitForSingleObject(pi.hProcess, 30000) == WAIT_TIMEOUT)
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen > 0) {
        output.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), &output[0], wlen);
    }
    return true;
}

std::vector<FlatLegacyItem> ParseEnumeration(const std::wstring& s) {
    std::vector<FlatLegacyItem> items;
    size_t lf = s.find(L'\n');
    if (lf == std::wstring::npos) return items;

    size_t pos = lf + 1;
    while (pos < s.size()) {
        size_t eol = s.find(L'\n', pos);
        std::wstring line = (eol == std::wstring::npos) ? s.substr(pos) : s.substr(pos, eol - pos);
        pos = (eol == std::wstring::npos) ? s.size() : eol + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();

        size_t tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        UINT offset = (UINT)wcstoul(line.substr(0, tab).c_str(), nullptr, 10);
        std::wstring title = line.substr(tab + 1);
        if (title.empty()) continue;

        FlatLegacyItem item;
        item.offset = offset;
        item.title = title;
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace

CLegacyCommand::CLegacyCommand() : _ref(1), _kind(Kind::Root), _offset(0) {}
CLegacyCommand::~CLegacyCommand() {}

HRESULT CLegacyCommand::Create(Kind kind,
                               const Microsoft::WRL::ComPtr<IShellItemArray>& selection,
                               const std::vector<std::wstring>& paths,
                               UINT offset, const std::wstring& title, const std::wstring& icon,
                               REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    auto* c = new (std::nothrow) CLegacyCommand();
    if (!c) return E_OUTOFMEMORY;
    c->_kind = kind;
    c->_selection = selection;
    c->_paths = paths;
    c->_offset = offset;
    c->_title = title;
    c->_icon = icon;
    HRESULT hr = c->QueryInterface(riid, ppv);
    c->Release();
    return hr;
}

STDMETHODIMP CLegacyCommand::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
        *ppv = static_cast<IExplorerCommand*>(this);
    } else if (riid == IID_IObjectWithSelection) {
        *ppv = static_cast<IObjectWithSelection*>(this);
    } else if (riid == IID_IShellExtInit) {
        *ppv = static_cast<IShellExtInit*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CLegacyCommand::AddRef() {
    return (ULONG)InterlockedIncrement(&_ref);
}

STDMETHODIMP_(ULONG) CLegacyCommand::Release() {
    LONG r = InterlockedDecrement(&_ref);
    if (r == 0) delete this;
    return (ULONG)r;
}

STDMETHODIMP CLegacyCommand::GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName) {
    if (!ppszName) return E_POINTER;
    if (!_selection && psiItemArray) _selection = psiItemArray;
    if (_kind == Kind::Root) return DuplicateString(L"Classic Menu", ppszName);
    return DuplicateString(_title, ppszName);
}

STDMETHODIMP CLegacyCommand::GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon) {
    if (!ppszIcon) return E_POINTER;
    if (!_selection && psiItemArray) _selection = psiItemArray;
    *ppszIcon = nullptr;

    if (_kind == Kind::Root) {
        if (!g_hInst) return S_FALSE;
        wchar_t path[MAX_PATH] = { 0 };
        if (!GetModuleFileNameW(g_hInst, path, MAX_PATH)) return S_FALSE;
        std::wstring icon = std::wstring(L"\"") + path + L"\",0";
        return DuplicateString(icon, ppszIcon);
    }
    if (!_icon.empty()) return DuplicateString(_icon, ppszIcon);
    return S_FALSE;
}

STDMETHODIMP CLegacyCommand::GetToolTip(IShellItemArray*, LPWSTR* ppszInfotip) {
    if (ppszInfotip) *ppszInfotip = nullptr;
    return S_FALSE;
}

STDMETHODIMP CLegacyCommand::GetCanonicalName(GUID* pguidCommandName) {
    if (!pguidCommandName) return E_POINTER;
    *pguidCommandName = GUID_RootCommand;
    if (_kind == Kind::Item) pguidCommandName->Data1 = GUID_RootCommand.Data1 + _offset + 1;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::GetState(IShellItemArray* psiItemArray, BOOL, EXPCMDSTATE* pCmdState) {
    if (!pCmdState) return E_POINTER;
    if (!_selection && psiItemArray) _selection = psiItemArray;
    *pCmdState = ECS_ENABLED;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::GetFlags(EXPCMDFLAGS* pFlags) {
    if (!pFlags) return E_POINTER;
    *pFlags = (_kind == Kind::Root) ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = nullptr;
    if (_kind != Kind::Root) return E_NOTIMPL;

    std::vector<std::wstring> paths;
    if (FAILED(ExtractPaths(_selection.Get(), paths)) || paths.empty()) {
        Log(L"EnumSubCommands: no selection paths");
        return CEnumExplorerCommand::Create({}, ppEnum);
    }

    std::vector<std::wstring> args;
    args.push_back(L"--enumerate");
    args.insert(args.end(), paths.begin(), paths.end());

    std::wstring output;
    if (!LaunchWorker(args, output)) {
        Log(L"EnumSubCommands: worker launch failed");
        return CEnumExplorerCommand::Create({}, ppEnum);
    }

    std::vector<FlatLegacyItem> items = ParseEnumeration(output);
    Log(L"EnumSubCommands: enumerated %zu items", items.size());

    std::vector<Microsoft::WRL::ComPtr<IExplorerCommand>> commands;
    commands.reserve(items.size());
    for (const auto& fi : items) {
        Microsoft::WRL::ComPtr<IExplorerCommand> cmd;
        if (SUCCEEDED(Create(Kind::Item, _selection, paths, fi.offset, fi.title, L"",
                             IID_IExplorerCommand, (void**)cmd.GetAddressOf())) && cmd) {
            commands.push_back(cmd);
        }
    }
    return CEnumExplorerCommand::Create(std::move(commands), ppEnum);
}

STDMETHODIMP CLegacyCommand::Invoke(IShellItemArray* psiItemArray, IBindCtx*) {
    if (_kind == Kind::Root) return S_OK;

    std::vector<std::wstring> paths;
    if (psiItemArray) ExtractPaths(psiItemArray, paths);
    if (paths.empty()) paths = _paths;
    if (paths.empty()) return E_FAIL;

    std::vector<std::wstring> args;
    args.push_back(L"--invoke");
    args.push_back(std::to_wstring(_offset));
    args.push_back(_title);
    args.insert(args.end(), paths.begin(), paths.end());

    std::wstring output;
    if (!LaunchWorker(args, output)) return E_FAIL;
    Log(L"Invoke: worker output=%s", output.c_str());

    if (output.find(L"NOTFOUND") == 0) {
        MessageBoxW(nullptr,
            L"The legacy menu order changed.\nPlease re-open the menu and try again.",
            L"LegacyMenuBridge", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return S_OK;
    }
    return (output.find(L"OK") == 0) ? S_OK : E_FAIL;
}

STDMETHODIMP CLegacyCommand::SetSelection(IShellItemArray* psia) {
    _selection = psia;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::GetSelection(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (!_selection) { *ppv = nullptr; return E_NOINTERFACE; }
    return _selection->QueryInterface(riid, ppv);
}

STDMETHODIMP CLegacyCommand::Initialize(PCIDLIST_ABSOLUTE, IDataObject* pdtobj, HKEY) {
    if (!pdtobj) return E_INVALIDARG;
    Microsoft::WRL::ComPtr<IShellItemArray> array;
    HRESULT hr = SHCreateShellItemArrayFromDataObject(pdtobj, IID_IShellItemArray,
                                                      (void**)array.GetAddressOf());
    if (SUCCEEDED(hr) && array) _selection = array;
    return S_OK;
}

} // namespace legacy
