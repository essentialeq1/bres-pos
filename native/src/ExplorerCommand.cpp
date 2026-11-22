#include "Common.h"
#include "ExplorerCommand.h"
#include "Guid.h"
#include "Log.h"
#include "MenuModel.h"
#include "Module.h"
#include "Snapshot.h"

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

// Worker process management, framing and caching live in Snapshot.cpp.


const wchar_t* KindStr(CLegacyCommand::Kind k) {
    switch (k) {
    case CLegacyCommand::Kind::Root: return L"root";
    case CLegacyCommand::Kind::SubMenu: return L"submenu";
    default: return L"item";
    }
}

} // namespace

CLegacyCommand::CLegacyCommand()
    : _ref(1), _kind(Kind::Root), _offset(0), _disabled(false) {}
CLegacyCommand::~CLegacyCommand() {}

HRESULT CLegacyCommand::Create(Kind kind,
                               const Microsoft::WRL::ComPtr<IShellItemArray>& selection,
                               const std::vector<std::wstring>& paths,
                               const LegacyMenuItem& node,
                               REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    auto* c = new (std::nothrow) CLegacyCommand();
    if (!c) return E_OUTOFMEMORY;
    c->_kind = kind;
    c->_selection = selection;
    c->_paths = paths;
    c->_offset = node.offset;
    c->_title = node.title;
    c->_verb = node.verb;
    c->_icon = node.icon;
    c->_children = node.children;
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
    Log(L"GetTitle(%s)", KindStr(_kind));
    if (!ppszName) return E_POINTER;
    if (!_selection && psiItemArray) _selection = psiItemArray;
    if (_kind == Kind::Root) return DuplicateString(L"Classic Menu", ppszName);
    return DuplicateString(_title, ppszName);
}

STDMETHODIMP CLegacyCommand::GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon) {
    Log(L"GetIcon(%s)", KindStr(_kind));
    if (!ppszIcon) return E_POINTER;
    if (!_selection && psiItemArray) _selection = psiItemArray;
    *ppszIcon = nullptr;

    // Per-item icon location (from GCS_VERBICONW) when we captured one.
    if (_kind == Kind::Item && !_icon.empty())
        return DuplicateString(_icon, ppszIcon);

    // The module icon doubles as the root icon and the fallback for items
    // without an icon. Never return S_FALSE here: the XAML menu crashes on it.
    if (g_hInst) {
        wchar_t path[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(g_hInst, path, MAX_PATH)) {
            std::wstring icon = std::wstring(L"\"") + path + L"\",0";
            return DuplicateString(icon, ppszIcon);
        }
    }
    return E_FAIL;
}

STDMETHODIMP CLegacyCommand::GetToolTip(IShellItemArray*, LPWSTR* ppszInfotip) {
    Log(L"GetToolTip(%s)", KindStr(_kind));
    if (ppszInfotip) *ppszInfotip = nullptr;
    return E_NOTIMPL;
}

STDMETHODIMP CLegacyCommand::GetCanonicalName(GUID* pguidCommandName) {
    Log(L"GetCanonicalName(%s)", KindStr(_kind));
    if (!pguidCommandName) return E_POINTER;
    *pguidCommandName = GUID_NULL;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::GetState(IShellItemArray* psiItemArray, BOOL, EXPCMDSTATE* pCmdState) {
    Log(L"GetState(%s)", KindStr(_kind));
    if (!pCmdState) return E_POINTER;
    if (!_selection && psiItemArray) _selection = psiItemArray;
    *pCmdState = _disabled ? ECS_DISABLED : ECS_ENABLED;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::GetFlags(EXPCMDFLAGS* pFlags) {
    Log(L"GetFlags(%s)", KindStr(_kind));
    if (!pFlags) return E_POINTER;
    *pFlags = (_kind == Kind::Item) ? ECF_DEFAULT : ECF_HASSUBCOMMANDS;
    return S_OK;
}

STDMETHODIMP CLegacyCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = nullptr;
    if (_kind == Kind::Item) return E_NOTIMPL;

    std::vector<std::wstring> paths;
    if (_selection) ExtractPaths(_selection.Get(), paths);
    if (paths.empty()) paths = _paths;

    std::vector<LegacyMenuItem> nodes;
    if (_kind == Kind::Root) {
        if (!paths.empty()) nodes = GetSnapshot(paths);
    } else {
        nodes = _children;   // SubMenu: its own nested children
    }

    std::vector<Microsoft::WRL::ComPtr<IExplorerCommand>> commands;
    commands.reserve(nodes.size() + 1);
    for (const auto& node : nodes) {
        const Kind kind = node.IsSubmenu() ? Kind::SubMenu : Kind::Item;
        Microsoft::WRL::ComPtr<IExplorerCommand> cmd;
        if (SUCCEEDED(Create(kind, _selection, paths, node, IID_IExplorerCommand,
                             (void**)cmd.GetAddressOf())) && cmd) {
            commands.push_back(cmd);
        }
    }

    // Safety net: a flyout with zero items loses its expand arrow in the
    // modern menu. Keep one disabled placeholder so the entry stays usable.
    if (commands.empty()) {
        LegacyMenuItem placeholder;
        placeholder.title = L"No classic items found";
        Microsoft::WRL::ComPtr<IExplorerCommand> cmd;
        if (SUCCEEDED(Create(Kind::Item, _selection, paths, placeholder,
                             IID_IExplorerCommand, (void**)cmd.GetAddressOf())) && cmd) {
            static_cast<CLegacyCommand*>(cmd.Get())->_disabled = true;
            commands.push_back(cmd);
        }
    }

    return CEnumExplorerCommand::Create(std::move(commands), ppEnum);
}

STDMETHODIMP CLegacyCommand::Invoke(IShellItemArray* psiItemArray, IBindCtx*) {
    if (_kind != Kind::Item) return S_OK;

    std::vector<std::wstring> paths;
    if (psiItemArray) ExtractPaths(psiItemArray, paths);
    if (paths.empty()) paths = _paths;
    if (paths.empty()) return E_FAIL;

    Log(L"Invoke: verb=%s offset=%u title=%s", _verb.c_str(), _offset, _title.c_str());
    HRESULT hr = InvokeLegacyItem(paths, _offset, _verb, _title);
    if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
        MessageBoxW(nullptr,
            L"The legacy menu order changed.\nPlease re-open the menu and try again.",
            L"LegacyMenuBridge", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
        return S_OK;
    }
    return hr;
}

STDMETHODIMP CLegacyCommand::SetSelection(IShellItemArray* psia) {
    _selection = psia;
    if (psia) {
        std::vector<std::wstring> paths;
        if (SUCCEEDED(ExtractPaths(psia, paths)) && !paths.empty()) _paths = paths;
    }
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
