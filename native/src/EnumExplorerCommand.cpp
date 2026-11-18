#include "Common.h"
#include "ExplorerCommand.h"

#include <new>

namespace legacy {

CEnumExplorerCommand::CEnumExplorerCommand() : _ref(1), _index(0) {}
CEnumExplorerCommand::~CEnumExplorerCommand() {}

HRESULT CEnumExplorerCommand::Create(std::vector<Microsoft::WRL::ComPtr<IExplorerCommand>> items,
                                     IEnumExplorerCommand** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = nullptr;
    auto* e = new (std::nothrow) CEnumExplorerCommand();
    if (!e) return E_OUTOFMEMORY;
    e->_items = std::move(items);
    *ppEnum = e;
    return S_OK;
}

STDMETHODIMP CEnumExplorerCommand::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IEnumExplorerCommand) {
        *ppv = static_cast<IEnumExplorerCommand*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CEnumExplorerCommand::AddRef() {
    return (ULONG)InterlockedIncrement(&_ref);
}

STDMETHODIMP_(ULONG) CEnumExplorerCommand::Release() {
    LONG r = InterlockedDecrement(&_ref);
    if (r == 0) delete this;
    return (ULONG)r;
}

STDMETHODIMP CEnumExplorerCommand::Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched) {
    if (!pUICommand) return E_POINTER;
    ULONG fetched = 0;
    while (fetched < celt && _index < _items.size()) {
        IExplorerCommand* cmd = _items[_index].Get();
        if (cmd) cmd->AddRef();
        pUICommand[fetched] = cmd;
        ++fetched;
        ++_index;
    }
    if (pceltFetched) *pceltFetched = fetched;
    return (fetched == celt) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumExplorerCommand::Skip(ULONG celt) {
    _index += celt;
    if (_index > _items.size()) _index = _items.size();
    return (_index < _items.size()) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumExplorerCommand::Reset() {
    _index = 0;
    return S_OK;
}

STDMETHODIMP CEnumExplorerCommand::Clone(IEnumExplorerCommand** ppenum) {
    if (!ppenum) return E_POINTER;
    *ppenum = nullptr;
    auto* e = new (std::nothrow) CEnumExplorerCommand();
    if (!e) return E_OUTOFMEMORY;
    e->_items = _items;
    e->_index = _index;
    *ppenum = e;
    return S_OK;
}

} // namespace legacy
