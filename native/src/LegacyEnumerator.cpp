#include "Common.h"
#include "LegacyEnumerator.h"
#include "Log.h"

#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>

namespace legacy {
namespace {

constexpr UINT kIdFirst = 1;
constexpr UINT kIdLast  = 0x7FFF;

struct MergedMenu {
    Microsoft::WRL::ComPtr<IContextMenu> ctx;
    HMENU hMenu = nullptr;
    std::vector<PIDLIST_ABSOLUTE> ownedPidls;

    ~MergedMenu() {
        if (hMenu) { DestroyMenu(hMenu); hMenu = nullptr; }
        for (auto p : ownedPidls) { if (p) ILFree(p); }
    }
};

HRESULT CreateMergedMenu(const std::vector<std::wstring>& paths, MergedMenu& out) {
    if (paths.empty()) return E_INVALIDARG;

    PIDLIST_ABSOLUTE pidlFirst = nullptr;
    HRESULT hr = SHParseDisplayName(paths[0].c_str(), nullptr, &pidlFirst, 0, nullptr);
    if (FAILED(hr) || !pidlFirst) return FAILED(hr) ? hr : E_FAIL;
    out.ownedPidls.push_back(pidlFirst);

    Microsoft::WRL::ComPtr<IShellFolder> parent;
    hr = SHBindToParent(pidlFirst, IID_IShellFolder, (void**)parent.GetAddressOf(), nullptr);
    if (FAILED(hr) || !parent) {
        Log(L"SHBindToParent failed hr=0x%08X", hr);
        return FAILED(hr) ? hr : E_FAIL;
    }

    std::vector<PCUITEMID_CHILD> children;
    for (const auto& path : paths) {
        PIDLIST_ABSOLUTE full = nullptr;
        if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &full, 0, nullptr)) || !full) continue;
        PCUITEMID_CHILD child = ILFindLastID(full);
        if (child) { children.push_back(child); out.ownedPidls.push_back(full); }
        else ILFree(full);
    }
    if (children.empty()) return E_FAIL;

    hr = parent->GetUIObjectOf(nullptr, (UINT)children.size(), children.data(),
                               IID_IContextMenu, nullptr,
                               (void**)out.ctx.GetAddressOf());
    if (FAILED(hr) || !out.ctx) {
        Log(L"GetUIObjectOf(IContextMenu) failed hr=0x%08X", hr);
        return FAILED(hr) ? hr : E_FAIL;
    }

    out.hMenu = CreatePopupMenu();
    if (!out.hMenu) return E_FAIL;

    // Match Explorer's classic-menu context so the same verbs appear.
    hr = out.ctx->QueryContextMenu(out.hMenu, 0, kIdFirst, kIdLast, CMF_NORMAL | CMF_EXPLORE);
    if (FAILED(hr)) {
        Log(L"QueryContextMenu failed hr=0x%08X", hr);
        return hr;
    }
    return S_OK;
}

void WalkMenu(MergedMenu& merged, HMENU menu, const std::wstring& prefix,
              std::vector<FlatLegacyItem>& out) {
    int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW mii = { 0 };
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        wchar_t buf[512] = { 0 };
        mii.dwTypeData = buf;
        mii.cch = _countof(buf);

        if (!GetMenuItemInfoW(menu, (UINT)i, MF_BYPOSITION, &mii)) continue;
        if (mii.fType & MFT_SEPARATOR) continue;

        std::wstring text = buf;
        bool inRange = (mii.wID >= kIdFirst && mii.wID <= kIdLast);

        if (text.empty() && inRange) {
            wchar_t verb[512] = { 0 };
            if (SUCCEEDED(merged.ctx->GetCommandString(mii.wID - kIdFirst, GCS_VERBW,
                    nullptr, (LPSTR)verb, _countof(verb))) && verb[0]) {
                text = verb;
            }
        }
        if (text.empty()) continue;

        std::wstring flat = prefix.empty() ? text : (prefix + L" \u203A " + text);

        if (mii.hSubMenu) {
            WalkMenu(merged, mii.hSubMenu, flat, out);
        } else if (inRange) {
            FlatLegacyItem item;
            item.title = flat;
            item.offset = mii.wID - kIdFirst;
            out.push_back(std::move(item));
        }
    }
}

} // namespace

HRESULT BuildSnapshot(const std::vector<std::wstring>& paths,
                      std::vector<FlatLegacyItem>& outItems) {
    outItems.clear();

    MergedMenu merged;
    HRESULT hr = CreateMergedMenu(paths, merged);
    if (FAILED(hr)) {
        Log(L"BuildSnapshot: CreateMergedMenu hr=0x%08X", hr);
        return hr;
    }

    WalkMenu(merged, merged.hMenu, L"", outItems);
    Log(L"BuildSnapshot: %zu path(s), %zu item(s) for %s",
        paths.size(), outItems.size(), paths[0].c_str());
    return S_OK;
}

HRESULT Invoke(const std::vector<std::wstring>& paths, UINT offset,
               const std::wstring& title) {
    MergedMenu merged;
    HRESULT hr = CreateMergedMenu(paths, merged);
    if (FAILED(hr)) return hr;

    std::vector<FlatLegacyItem> current;
    WalkMenu(merged, merged.hMenu, L"", current);

    UINT chosen = 0;
    bool found = false;
    for (const auto& it : current) {
        if (it.offset == offset && it.title == title) { chosen = it.offset; found = true; break; }
    }
    if (!found) {
        for (const auto& it : current) {
            if (it.title == title) { chosen = it.offset; found = true; break; }
        }
    }
    if (!found) {
        Log(L"invoke: no match for offset=%u title=%s (order changed)", offset, title.c_str());
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    CMINVOKECOMMANDINFOEX info = { 0 };
    info.cbSize = sizeof(info);
    info.fMask = 0;
    info.lpVerb = MAKEINTRESOURCEA(chosen);
    info.lpVerbW = nullptr;
    info.nShow = SW_SHOWNORMAL;

    hr = merged.ctx->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
    Log(L"invoke: offset=%u -> hr=0x%08X", chosen, hr);
    return hr;
}

} // namespace legacy
