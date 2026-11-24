#include "Common.h"
#include "ExplorerCommand.h"
#include "Guid.h"
#include "Log.h"
#include "Module.h"
#include "Snapshot.h"

#include <new>

HINSTANCE g_hInst = nullptr;
static LONG g_objectCount = 0;

namespace {

class CClassFactory final : public IClassFactory {
public:
    CClassFactory() : _ref(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return (ULONG)InterlockedIncrement(&_ref);
    }

    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        // Prewarm the worker process (once) so the first right-click isn't cold.
        // Not done in DllMain: spawning a process there would risk the loader lock.
        legacy::StartSnapshotWorkerPrewarm();
        legacy::LegacyMenuItem root;
        root.title = L"Classic Menu";
        return legacy::CLegacyCommand::Create(legacy::CLegacyCommand::Kind::Root,
                                              nullptr, {}, root, riid, ppv);
    }

    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) InterlockedIncrement(&g_objectCount);
        else InterlockedDecrement(&g_objectCount);
        return S_OK;
    }

private:
    LONG _ref;
};

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = module;
        DisableThreadLibraryCalls(module);
        legacy::SetLogComponent(L"dll");
        wchar_t host[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, host, MAX_PATH);
        legacy::Log(L"host process: %s", host);
        legacy::Log(L"dll attached");
        legacy::TimingLog(L"DLL_PROCESS_ATTACH host=%s", host);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(rclsid, CLSID_LegacyMenuCommand)) return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new (std::nothrow) CClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    // Stay resident so the legacy handler DLLs we loaded remain warm.
    return S_FALSE;
}
