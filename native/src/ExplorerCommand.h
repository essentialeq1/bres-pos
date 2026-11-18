#pragma once
#include "Common.h"
#include "MenuModel.h"

namespace legacy {

class CEnumExplorerCommand final : public IEnumExplorerCommand {
public:
    CEnumExplorerCommand();

    static HRESULT Create(std::vector<Microsoft::WRL::ComPtr<IExplorerCommand>> items,
                          IEnumExplorerCommand** ppEnum);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Next(ULONG celt, IExplorerCommand** pUICommand, ULONG* pceltFetched) override;
    STDMETHODIMP Skip(ULONG celt) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Clone(IEnumExplorerCommand** ppenum) override;

private:
    ~CEnumExplorerCommand();
    LONG _ref;
    size_t _index;
    std::vector<Microsoft::WRL::ComPtr<IExplorerCommand>> _items;
};

class CLegacyCommand final : public IExplorerCommand,
                             public IObjectWithSelection,
                             public IShellExtInit {
public:
    enum class Kind { Root, Item };

    CLegacyCommand();

    static HRESULT Create(Kind kind,
                          const Microsoft::WRL::ComPtr<IShellItemArray>& selection,
                          UINT offset,
                          const std::wstring& title,
                          const std::wstring& icon,
                          REFIID riid,
                          void** ppv);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetTitle(IShellItemArray* psiItemArray, LPWSTR* ppszName) override;
    STDMETHODIMP GetIcon(IShellItemArray* psiItemArray, LPWSTR* ppszIcon) override;
    STDMETHODIMP GetToolTip(IShellItemArray* psiItemArray, LPWSTR* ppszInfotip) override;
    STDMETHODIMP GetCanonicalName(GUID* pguidCommandName) override;
    STDMETHODIMP GetState(IShellItemArray* psiItemArray, BOOL fOkToBeSlow, EXPCMDSTATE* pCmdState) override;
    STDMETHODIMP Invoke(IShellItemArray* psiItemArray, IBindCtx* pbc) override;
    STDMETHODIMP GetFlags(EXPCMDFLAGS* pFlags) override;
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum) override;

    STDMETHODIMP SetSelection(IShellItemArray* psia) override;
    STDMETHODIMP GetSelection(REFIID riid, void** ppv) override;

    STDMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj, HKEY hkeyProgID) override;

private:
    ~CLegacyCommand();
    LONG _ref;
    Kind _kind;
    UINT _offset;
    std::wstring _title;
    std::wstring _icon;
    Microsoft::WRL::ComPtr<IShellItemArray> _selection;
};

} // namespace legacy
