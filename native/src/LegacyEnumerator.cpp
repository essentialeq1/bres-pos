#include "Common.h"
#include "LegacyEnumerator.h"
#include "Log.h"

#include <shlobj.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <winreg.h>

#include <cwchar>
#include <cwctype>

// Some SDK builds hide this from WIN32_LEAN_AND_MEAN.
#ifndef GCS_VERBICONW
#define GCS_VERBICONW 0x00000014
#endif

namespace legacy {
namespace {

constexpr UINT kIdFirst = 1;
constexpr UINT kIdLast  = 0x7FFF;
constexpr int  kMaxDepth = 12;

// SEE_MASK_UNICODE (aliased by shlobj.h as CMIC_MASK_UNICODE). shlobj.h defines
// CMIC_MASK_UNICODE in terms of SEE_MASK_UNICODE, which is not visible under
// WIN32_LEAN_AND_MEAN, so we use the documented bit value directly.
constexpr UINT kInvokeUnicodeFlag = 0x00004000;

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

// ---- shell-item blocklist: items the Windows 11 modern menu already shows. ---
// A menu item is filtered only when its canonical verb OR its normalized title
// matches an entry (case-insensitive). Only true duplicates belong here, so the
// modern menu never hides a classic-only action (Print, Edit, Play, Preview,
// Rotate, Restore previous versions, Set as desktop background, ...). The list
// lives in LegacyMenuBridge.blocklist.txt next to the worker EXE; the in-code
// default below is the fallback when that file is missing.

std::wstring LowerWide(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
    return r;
}

std::wstring TrimWide(const std::wstring& s) {
    size_t b = 0;
    size_t e = s.size();
    auto isSpace = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\u00a0' || c == L'\u2007';
    };
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Strip Win32 display decoration so two menu titles compare reliably:
// '&' accelerator mnemonics, tab-aligned suffix hints, trailing "..." / "..."
// ellipses, surrounding whitespace — then lowercase.
std::wstring NormalizeTitle(const std::wstring& raw) {
    std::wstring s = raw;
    const size_t tab = s.find(L'\t');
    if (tab != std::wstring::npos) s = s.substr(0, tab);
    while (s.size() >= 3 && s.compare(s.size() - 3, 3, L"...") == 0)
        s.erase(s.size() - 3);
    while (!s.empty() && s.back() == L'\u2026') s.pop_back();

    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) if (c != L'&') out.push_back(c);
    return LowerWide(TrimWide(out));
}

std::vector<std::wstring> DefaultBlocklist() {
    return {
        // verbs (true duplicates the modern menu already offers)
        L"open", L"openas", L"cut", L"copy", L"paste", L"delete", L"rename",
        L"properties", L"copyaspath", L"sendto", L"share", L"windows.share",
        L"modernsharing", L"troubleshootcompatibility", L"link",
        L"pin", L"unpin", L"pinfromstart", L"pintostartscreen", L"pintotaskbar",
        L"pintohome", L"pintoquickaccess", L"unpinfromstart",
        L"unpinfromtaskbar", L"unpinfromhome", L"unpinfromquickaccess",
        L"windows.unpinfromquickaccess",
        // titles (normalized, multi-word)
        L"open with", L"copy as path", L"send to", L"troubleshoot compatibility",
        L"pin to start", L"pin to taskbar", L"pin to quick access",
        L"create shortcut", L"give access to", L"compress to zip file",
        L"extract all",
    };
}

std::vector<std::wstring> g_blocklist = DefaultBlocklist();

const std::vector<std::wstring>& Blocklist() { return g_blocklist; }

std::wstring Utf8ToWideStr(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

void LoadBlocklistFromFileImpl() {
    wchar_t dir[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *slash = 0;
    const std::wstring path = std::wstring(dir) + L"\\LegacyMenuBridge.blocklist.txt";

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;   // keep the built-in default

    DWORD size = GetFileSize(h, nullptr);
    std::string bytes;
    if (size != INVALID_FILE_SIZE && size > 0) {
        bytes.resize(size);
        DWORD read = 0;
        if (!ReadFile(h, &bytes[0], size, &read, nullptr)) bytes.clear();
        else bytes.resize(read);
    }
    CloseHandle(h);
    if (bytes.empty()) return;

    std::vector<std::wstring> entries;
    size_t pos = 0;
    while (pos < bytes.size()) {
        size_t eol = pos;
        while (eol < bytes.size() && bytes[eol] != '\n') ++eol;
        std::string line(bytes.data() + pos, eol - pos);
        pos = eol + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t b = 0, e = line.size();
        while (b < e && (line[b] == ' ' || line[b] == '\t')) ++b;
        while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
        line = line.substr(b, e - b);
        if (line.empty() || line[0] == '#') continue;

        // skip a UTF-8 BOM if present
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            line = line.substr(3);

        std::wstring entry = TrimWide(Utf8ToWideStr(line));
        if (!entry.empty()) entries.push_back(std::move(entry));
    }

    if (!entries.empty()) g_blocklist = std::move(entries);
}

bool IsShellBuiltin(const std::wstring& verb, const std::wstring& title) {
    const std::wstring nverb = LowerWide(TrimWide(verb));
    const std::wstring ntitle = NormalizeTitle(title);
    for (const auto& e : Blocklist()) {
        const std::wstring ne = LowerWide(TrimWide(e));
        if (ne.empty()) continue;
        if (!nverb.empty() && ne == nverb) return true;
        if (!ntitle.empty() && ne == ntitle) return true;
    }
    return false;
}

// ---- per-item icon resolution --------------------------------------------
// GCS_VERBICONW frequently returns a bare relative name ("imageres.dll,-78")
// or an unquoted path with spaces. The modern menu only resolves absolute,
// properly-quoted "path,index" locations, so normalize everything first.
std::wstring NormalizeIconLocation(const std::wstring& raw) {
    std::wstring s = raw;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        s = s.substr(1, s.size() - 2);
    if (s.empty()) return L"";

    std::wstring path = s;
    std::wstring index;
    const size_t comma = s.find_last_of(L',');
    if (comma != std::wstring::npos) {
        const std::wstring tail = s.substr(comma + 1);
        bool isIndex = !tail.empty();
        bool hasDigit = false;
        for (wchar_t c : tail) {
            if (c >= L'0' && c <= L'9') hasDigit = true;
            else if (c != L'-') { isIndex = false; break; }
        }
        if (isIndex && hasDigit) {
            path = s.substr(0, comma);
            index = tail;
        }
    }
    if (index.empty()) index = L"0";

    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);

    if (path.find(L'%') != std::wstring::npos) {
        wchar_t buf[2048] = { 0 };
        const DWORD n = ExpandEnvironmentStringsW(path.c_str(), buf, _countof(buf));
        if (n > 0 && n <= _countof(buf)) path = buf;
    }

    const bool absolute = (path.size() >= 2 && path[1] == L':') ||
                          (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
    if (!absolute) {
        wchar_t sys[MAX_PATH] = { 0 };
        if (GetSystemDirectoryW(sys, MAX_PATH))
            path = (path.size() >= 1 && path[0] == L'\\')
                     ? std::wstring(sys) + path
                     : std::wstring(sys) + L"\\" + path;
    }

    if (path.find(L' ') != std::wstring::npos &&
        !(path.size() >= 2 && path.front() == L'"' && path.back() == L'"'))
        path = L"\"" + path + L"\"";

    return path + L"," + index;
}

std::wstring RegReadIconValue(const std::wstring& subkey) {
    wchar_t buf[2048] = { 0 };
    DWORD cb = sizeof(buf);
    const LSTATUS st = RegGetValueW(HKEY_CLASSES_ROOT, subkey.c_str(), L"Icon",
                                    RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
                                    nullptr, buf, &cb);
    if (st != ERROR_SUCCESS) return L"";
    return buf;
}

// Static verbs register their icon under "shell\<verb>" (the "Icon" value).
// This recovers icons (e.g. the "Take ownership" shield) when the merged menu's
// GCS_VERBICONW answer is empty or only a bare name.
std::wstring ResolveVerbIconFromRegistry(const std::wstring& verb,
                                         const std::wstring& firstPath) {
    if (verb.empty()) return L"";

    if (!firstPath.empty()) {
        wchar_t progid[512] = { 0 };
        DWORD cch = _countof(progid);
        if (SUCCEEDED(AssocQueryStringW(0, ASSOCSTR_PROGID, firstPath.c_str(),
                                        nullptr, progid, &cch)) &&
            cch > 0 && progid[0]) {
            std::wstring value =
                RegReadIconValue(std::wstring(progid) + L"\\shell\\" + verb);
            if (!value.empty()) return NormalizeIconLocation(value);
        }
    }

    static const wchar_t* kRoots[] = {
        L"*\\shell\\", L"Directory\\shell\\", L"Drive\\shell\\",
        L"Folder\\shell\\", L"AllFilesystemObjects\\shell\\",
    };
    for (const auto* root : kRoots) {
        std::wstring value = RegReadIconValue(std::wstring(root) + verb);
        if (!value.empty()) return NormalizeIconLocation(value);
    }
    return L"";
}

std::wstring ShieldIconLocation() {
    wchar_t sys[MAX_PATH] = { 0 };
    if (!GetSystemDirectoryW(sys, MAX_PATH)) return L"";
    return std::wstring(sys) + L"\\imageres.dll,-78";
}

std::wstring ResolveItemIcon(IContextMenu* ctx, UINT offset,
                             const std::wstring& verb,
                             const std::wstring& firstPath,
                             std::wstring& source) {
    std::wstring icon =
        NormalizeIconLocation(GetCommandStringText(ctx, offset, GCS_VERBICONW));
    if (!icon.empty()) { source = L"verb"; return icon; }

    icon = ResolveVerbIconFromRegistry(verb, firstPath);
    if (!icon.empty()) { source = L"registry"; return icon; }

    // Elevated verbs ("run as administrator", "take ownership") draw the same
    // UAC shield; synthesize it when nothing else provided an icon.
    if (EqualsI(verb, L"runas") || EqualsI(verb, L"runasuser")) {
        source = L"runas";
        return ShieldIconLocation();
    }
    source = L"none";
    return L"";
}

void WalkMenu(MergedMenu& merged, HMENU menu, int depth, bool hideShell,
              const std::wstring& firstPath, std::vector<LegacyMenuItem>& out) {
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
            // Hide shell-owned submenus ("Send to", "Give access to", "Open
            // with", ...) at the top level, keeping app submenus intact.
            if (hideShell && depth == 0 && IsShellBuiltin(verb, text)) continue;

            std::vector<LegacyMenuItem> kids;
            WalkMenu(merged, mii.hSubMenu, depth + 1, hideShell, firstPath, kids);
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
        if (inRange) {
            std::wstring source = L"none";
            node.icon = ResolveItemIcon(merged.ctx.Get(), offset, verb, firstPath, source);
            node.iconSource = source;
        }
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

void LoadShellBlocklistFromFile() { LoadBlocklistFromFileImpl(); }

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

    const std::wstring firstPath = paths.empty() ? L"" : paths[0];
    WalkMenu(merged, merged.hMenu, 0, hideShellBuiltins, firstPath, outItems);
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
        info.fMask = kInvokeUnicodeFlag;
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
    WalkMenu(merged, merged.hMenu, 0, false, paths.empty() ? L"" : paths[0], current);

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
