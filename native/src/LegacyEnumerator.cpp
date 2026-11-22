#include "Common.h"
#include "LegacyEnumerator.h"
#include "Log.h"

#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>

#include <cwchar>

// Some SDK builds hide this from WIN32_LEAN_AND_MEAN.
#ifndef GCS_VERBICONW
#define GCS_VERBICONW 0x00000014
#endif

namespace legacy {
namespace {

constexpr UINT kIdFirst = 1;
constexpr UINT kIdLast  = 0x7FFF;
constexpr int  kMaxDepth = 12;

struct MergedMenu {
    Microsoft::WRL::ComPtr<IContextMenu>  ctx;
    Microsoft::WRL::ComPtr<IContextMenu2> ctx2;
    Microsoft::WRL::ComPtr<IContextMenu3> ctx3;
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

    // IContextMenu2/3 are what let lazily-built submenus (Send to, some app
    // submenus) populate when we forward WM_INITMENUPOPUP below.
    out.ctx.As(&out.ctx2);
    out.ctx.As(&out.ctx3);

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

// Ask the handler to populate a menu (the root or any submenu). Without this,
// submenus such as "Send to" stay empty because Explorer only sends
// WM_INITMENUPOPUP when a submenu is actually opened.
void SendInit(MergedMenu& merged, HMENU hMenu) {
    if (!hMenu) return;
    if (merged.ctx3) {
        LRESULT result = 0;
        merged.ctx3->HandleMenuMsg2(WM_INITMENUPOPUP,
                                    reinterpret_cast<WPARAM>(hMenu),
                                    reinterpret_cast<LPARAM>(hMenu), &result);
    } else if (merged.ctx2) {
        merged.ctx2->HandleMenuMsg(WM_INITMENUPOPUP,
                                   reinterpret_cast<WPARAM>(hMenu),
                                   reinterpret_cast<LPARAM>(hMenu));
    }
}

std::wstring GetCommandStringText(IContextMenu* ctx, UINT offset, UINT type) {
    if (!ctx) return L"";
    wchar_t buf[1024] = { 0 };
    HRESULT hr = ctx->GetCommandString(offset, type, nullptr,
                                       reinterpret_cast<LPSTR>(buf), _countof(buf));
    if (SUCCEEDED(hr) && buf[0]) return buf;
    return L"";
}

bool EqualsI(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// Shell items that already exist in the Windows 11 modern menu. Hidden only at
// the top level, never inside submenus (so "Send to" and app submenus keep
// every entry).
bool IsShellBuiltin(const std::wstring& verb, const std::wstring& title) {
    static const wchar_t* kVerbs[] = {
        L"open", L"openas", L"edit", L"play", L"print", L"printto", L"runas",
        L"find", L"cut", L"copy", L"paste", L"pasteshortcut", L"delete",
        L"rename", L"properties", L"link", L"pin", L"unpin",
        L"pintostartscreen", L"pintotaskbar", L"pintohome",
        L"pintoquickaccess", L"share", L"windows.share", L"cast",
        L"casttodevice", L"addtoplaylist", L"includelibrary",
        L"rotateright", L"rotateleft", L"previousversions",
        L"restorepreviousversions", L"undo",
    };
    for (auto* v : kVerbs) if (EqualsI(verb, v)) return true;

    static const wchar_t* kTitles[] = {
        L"Copy as path", L"Give access to", L"Cast to Device",
        L"Restore previous versions", L"Pin to Start", L"Pin to taskbar",
        L"Pin to Quick access", L"Create shortcut", L"Open", L"Open with",
    };
    for (auto* t : kTitles) if (EqualsI(title, t)) return true;
    return false;
}

void WalkMenu(MergedMenu& merged, HMENU menu, int depth, bool hideShell,
              std::vector<LegacyMenuItem>& out) {
    if (!menu || depth > kMaxDepth) return;

    // Let the handler add lazy items (Send to, dynamic submenus) first.
    SendInit(merged, menu);

    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW mii = { 0 };
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        wchar_t buf[1024] = { 0 };
        mii.dwTypeData = buf;
        mii.cch = _countof(buf);

        if (!GetMenuItemInfoW(menu, (UINT)i, MF_BYPOSITION, &mii)) continue;
        if (mii.fType & MFT_SEPARATOR) continue;

        const bool inRange = (mii.wID >= kIdFirst && mii.wID <= kIdLast);
        const UINT offset = inRange ? (UINT)(mii.wID - kIdFirst) : 0;

        std::wstring text = buf;
        std::wstring verb;
        if (inRange) verb = GetCommandStringText(merged.ctx.Get(), offset, GCS_VERBW);
        if (text.empty()) text = verb;            // owner-draw / unnamed items
        if (text.empty()) continue;

        if (mii.hSubMenu) {
            std::vector<LegacyMenuItem> kids;
            WalkMenu(merged, mii.hSubMenu, depth + 1, hideShell, kids);
            if (kids.empty()) continue;           // don't show empty submenus

            LegacyMenuItem node;
            node.title = text;
            node.verb = verb;
            node.offset = offset;
            node.children = std::move(kids);
            out.push_back(std::move(node));
            continue;
        }

        // Keep only items we can actually invoke.
        if (!inRange && verb.empty()) continue;
        // Hide shell duplicates only at the top level.
        if (hideShell && depth == 0 && IsShellBuiltin(verb, text)) continue;

        LegacyMenuItem node;
        node.title = text;
        node.verb = verb;
        node.offset = offset;
        if (inRange) node.icon = GetCommandStringText(merged.ctx.Get(), offset, GCS_VERBICONW);
        out.push_back(std::move(node));
    }
}

size_t CountItems(const std::vector<LegacyMenuItem>& items) {
    size_t n = 0;
    for (const auto& it : items) n += 1 + CountItems(it.children);
    return n;
}

const LegacyMenuItem* FindByOffset(const std::vector<LegacyMenuItem>& items,
                                   UINT offset, const std::wstring& title) {
    for (const auto& it : items) {
        if (it.IsSubmenu()) {
            if (const auto* f = FindByOffset(it.children, offset, title)) return f;
        } else if (it.offset == offset && it.title == title) {
            return &it;
        }
    }
    return nullptr;
}

const LegacyMenuItem* FindByTitle(const std::vector<LegacyMenuItem>& items,
                                  const std::wstring& title) {
    for (const auto& it : items) {
        if (it.IsSubmenu()) {
            if (const auto* f = FindByTitle(it.children, title)) return f;
        } else if (it.title == title) {
            return &it;
        }
    }
    return nullptr;
}

HWND InvokeOwnerWindow() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) hwnd = GetShellWindow();
    return hwnd;
}

} // namespace

HRESULT BuildSnapshot(const std::vector<std::wstring>& paths,
                      bool hideShellBuiltins,
                      std::vector<LegacyMenuItem>& outItems) {
    outItems.clear();

    MergedMenu merged;
    HRESULT hr = CreateMergedMenu(paths, merged);
    if (FAILED(hr)) {
        Log(L"BuildSnapshot: CreateMergedMenu hr=0x%08X", hr);
        return hr;
    }

    WalkMenu(merged, merged.hMenu, 0, hideShellBuiltins, outItems);
    Log(L"BuildSnapshot: %zu path(s), %zu item(s) for %s",
        paths.size(), CountItems(outItems), paths[0].c_str());
    return S_OK;
}

HRESULT Invoke(const std::vector<std::wstring>& paths, UINT offset,
               const std::wstring& verb, const std::wstring& title) {
    MergedMenu merged;
    HRESULT hr = CreateMergedMenu(paths, merged);
    if (FAILED(hr)) return hr;

    // Preferred path: the handler exposed a canonical verb, so invoke by the
    // verb string. This is what makes Copy/Cut/Delete/Properties/Pin to Start
    // (and similar static verbs) work reliably; an offset alone is not valid
    // for many of them.
    if (!verb.empty()) {
        std::string verbA;
        int need = WideCharToMultiByte(CP_ACP, 0, verb.c_str(), (int)verb.size(),
                                       nullptr, 0, nullptr, nullptr);
        if (need > 0) {
            verbA.resize(need);
            WideCharToMultiByte(CP_ACP, 0, verb.c_str(), (int)verb.size(),
                                &verbA[0], need, nullptr, nullptr);
        }

        CMINVOKECOMMANDINFOEX info = { 0 };
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE;
        info.hwnd = InvokeOwnerWindow();
        info.lpVerb = verbA.empty() ? nullptr : verbA.c_str();
        info.lpVerbW = verb.c_str();
        info.nShow = SW_SHOWNORMAL;

        hr = merged.ctx->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
        Log(L"invoke: verb=%s -> hr=0x%08X", verb.c_str(), hr);
        if (SUCCEEDED(hr)) return hr;
        // Some handlers expose a verb but only honour offset invocation in
        // practice; fall through to the offset path below rather than failing.
    }

    // Fallback for handlers that expose no verb: rebuild, find the item by
    // offset+title (or by title alone), then invoke by its command offset.
    std::vector<LegacyMenuItem> current;
    WalkMenu(merged, merged.hMenu, 0, false, current);

    const LegacyMenuItem* hit = FindByOffset(current, offset, title);
    if (!hit) hit = FindByTitle(current, title);
    if (!hit) {
        Log(L"invoke: no match for offset=%u title=%s (order changed)", offset, title.c_str());
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    CMINVOKECOMMANDINFOEX info = { 0 };
    info.cbSize = sizeof(info);
    info.fMask = 0;
    info.hwnd = InvokeOwnerWindow();
    info.lpVerb = MAKEINTRESOURCEA(hit->offset);
    info.nShow = SW_SHOWNORMAL;

    hr = merged.ctx->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
    Log(L"invoke: offset=%u title=%s -> hr=0x%08X", hit->offset, hit->title.c_str(), hr);
    return hr;
}

} // namespace legacy
