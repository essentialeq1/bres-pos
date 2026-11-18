#include "Common.h"
#include "ExplorerCommand.h"
#include "Guid.h"
#include "LegacyEnumerator.h"
#include "Log.h"
#include "Module.h"

#include <cwchar>
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

} // namespace

CLegacyCommand::CLegacyCommand() : _ref(1), _kind(Kind::Root), _offset(0) {}
CLegacyCommand::~CLegacyCommand() {}

HRESULT CLegacyCommand::Create(Kind kind,
                               const Microsoft::WRL::ComPtr<IShellItemArray>& selection,
                               UINT offset, const std::wstring& title, const std::wstring& icon,
                               REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    auto* c = new (std::nothrow) CLegacyCommand();
    if (!c) return E_OUTOFMEMORY;
    c->_kind = kind;
    c->_selection = selection;
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
    if (_kind == Kind::Item) {
        pguidCommandName->Data1 = GUID_RootCommand.Data1 + _offset + 1;
    }
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

    std::vector<FlatLegacyItem> items;
    HRESULT hr = BuildLegacyMenuSnapshot(_selection.Get(), items);
    Log(L"EnumSubCommands: snapshot hr=0x%08X items=%zu", hr, items.size());

    std::vector<Microsoft::WRL::ComPtr<IExplorerCommand>> commands;
    commands.reserve(items.size());
    for (const auto& fi : items) {
        Microsoft::WRL::ComPtr<IExplorerCommand> cmd;
        if (SUCCEEDED(CLegacyCommand::Create(Kind::Item, _selection, fi.offset, fi.title,
                                             fi.iconPath, IID_IExplorerCommand,
                                             (void**)cmd.GetAddressOf())) && cmd) {
            commands.push_back(cmd);
        }
    }
    return CEnumExplorerCommand::Create(std::move(commands), ppEnum);
}

STDMETHODIMP CLegacyCommand::Invoke(IShellItemArray* psiItemArray, IBindCtx*) {
    if (_kind == Kind::Root) return S_OK;

    IShellItemArray* selection = psiItemArray ? psiItemArray : _selection.Get();
    if (!selection) return E_FAIL;

    HRESULT hr = InvokeLegacyItem(selection, _offset, _title);
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
