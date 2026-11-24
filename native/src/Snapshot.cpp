#include "Common.h"
#include "Snapshot.h"
#include "Log.h"
#include "Module.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <jobapi2.h>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace legacy {
namespace {

constexpr DWORD kMaxFrame = 32u * 1024u * 1024u;
constexpr wchar_t kFieldSep = L'\x1F';
constexpr char kFieldSepA = '\x1F';

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                        &out[0], n, nullptr, nullptr);
    return out;
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

bool WriteAll(HANDLE h, const void* buf, DWORD n) {
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

// (worker-side blocking read lives in main.cpp)

// Polled read so a hung/dead worker can never block the caller indefinitely.
bool ReadExactTimeout(HANDLE h, void* buf, DWORD n, DWORD timeoutMs) {
    char* p = static_cast<char*>(buf);
    DWORD done = 0;
    DWORD start = GetTickCount();
    while (done < n) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
        if (avail == 0) {
            if ((GetTickCount() - start) >= timeoutMs) return false;
            Sleep(3);
            continue;
        }
        DWORD toRead = n - done;
        if (toRead > avail) toRead = avail;
        DWORD read = 0;
        if (!ReadFile(h, p + done, toRead, &read, nullptr) || read == 0) return false;
        done += read;
        start = GetTickCount(); // progress resets the timeout
    }
    return true;
}

bool WriteFrame(HANDLE h, const std::string& payload) {
    DWORD len = (DWORD)payload.size();
    if (!WriteAll(h, &len, sizeof(len))) return false;
    if (len && !WriteAll(h, payload.data(), len)) return false;
    return true;
}

bool ReadFrame(HANDLE h, std::string& payload, DWORD timeoutMs) {
    DWORD len = 0;
    if (!ReadExactTimeout(h, &len, sizeof(len), timeoutMs)) return false;
    if (len > kMaxFrame) return false;
    payload.assign((size_t)len, '\0');
    if (len && !ReadExactTimeout(h, &payload[0], len, timeoutMs)) return false;
    return true;
}

// A kill-on-close job so the worker never outlives the host (dllhost.exe).
HANDLE EnsureKillJob() {
    static HANDLE job = CreateJobObjectW(nullptr, nullptr);
    static bool configured = false;
    if (job && !configured) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = { 0 };
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        configured = SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                             &info, sizeof(info)) != FALSE;
    }
    return job;
}

// A single long-lived worker process. Keeping it alive keeps every legacy
// context-menu handler DLL loaded, so menus after the first one are built
// almost instantly (and other modern extensions are no longer delayed).
struct WorkerClient {
    HANDLE hProcess = nullptr;
    HANDLE hThread = nullptr;
    HANDLE hStdin = nullptr;
    HANDLE hStdout = nullptr;

    bool IsAlive() const {
        return hProcess && WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT;
    }

    void Stop(bool terminate) {
        if (hStdin) { CloseHandle(hStdin); hStdin = nullptr; }
        if (hStdout) { CloseHandle(hStdout); hStdout = nullptr; }
        if (terminate && hProcess) TerminateProcess(hProcess, 1);
        if (hThread) { CloseHandle(hThread); hThread = nullptr; }
        if (hProcess) { CloseHandle(hProcess); hProcess = nullptr; }
    }

    bool Start(const wchar_t* mode) {
        Stop(true);
        ScopedTimer startTimer(L"worker: start process");

        std::wstring exe = WorkerExePath();
        if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log(L"worker exe missing: %s", exe.c_str());
            return false;
        }

        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
        if (!CreatePipe(&inR, &inW, &sa, 0)) return false;
        if (!CreatePipe(&outR, &outW, &sa, 0)) {
            CloseHandle(inR); CloseHandle(inW);
            return false;
        }
        SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

        // Send the child's stderr to NUL so stray debug output can never
        // corrupt the framed protocol stream.
        HANDLE hNul = CreateFileW(L"NUL", GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                  OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inR;
        si.hStdOutput = outW;
        si.hStdError = (hNul != INVALID_HANDLE_VALUE) ? hNul : outW;

        std::wstring cmd = L"\"" + exe + L"\" " + mode;
        PROCESS_INFORMATION pi = { 0 };
        BOOL ok = CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(inR);
        CloseHandle(outW);
        if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
        if (!ok) {
            Log(L"CreateProcess(%s) failed err=%lu", mode, (unsigned long)GetLastError());
            CloseHandle(inW); CloseHandle(outR);
            return false;
        }

        hProcess = pi.hProcess;
        hThread = pi.hThread;
        hStdin = inW;
        hStdout = outR;

        // Kill the worker automatically when this host process goes away.
        HANDLE job = EnsureKillJob();
        if (job) AssignProcessToJobObject(job, hProcess);
        return true;
    }

    bool EnsureStarted() {
        if (IsAlive() && hStdin && hStdout) return true;
        return Start(L"--serve");
    }
};

WorkerClient g_worker;
std::mutex g_workerMutex;
std::atomic<bool> g_persistentBusy{false};

// Fallback: one short-lived process that handles exactly one request.
bool TransactOneShot(const std::string& req, std::string& resp, DWORD timeoutMs) {
    WorkerClient one;
    if (!one.Start(L"--oneshot")) return false;
    ScopedTimer timer(L"worker: one-shot transact");
    bool ok = false;
    if (WriteFrame(one.hStdin, req)) {
        // Close stdin so the worker exits after answering.
        CloseHandle(one.hStdin);
        one.hStdin = nullptr;
        ok = ReadFrame(one.hStdout, resp, timeoutMs);
    }
    one.Stop(true);
    return ok;
}

bool TransactPersistent(const std::string& req, std::string& resp, DWORD timeoutMs) {
    if (!g_worker.EnsureStarted()) return false;
    ScopedTimer timer(L"worker: transact");
    if (WriteFrame(g_worker.hStdin, req) && ReadFrame(g_worker.hStdout, resp, timeoutMs))
        return true;
    Log(L"persistent worker transaction failed; restarting worker");
    g_worker.Stop(true);
    return false;
}

// Persistent (warm) worker for the common case; concurrent requests get their
// own one-shot worker so a slow legacy handler can't queue behind the user's
// next click. "Skip stale" is handled by the per-key PendingBuild guard, which
// never builds the same key twice at once.
bool Transact(const std::string& req, std::string& resp, DWORD timeoutMs) {
    bool expected = false;
    if (g_persistentBusy.compare_exchange_strong(expected, true)) {
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(g_workerMutex);
            ok = TransactPersistent(req, resp, timeoutMs);
        }
        g_persistentBusy.store(false);
        if (ok) return true;
    }
    return TransactOneShot(req, resp, timeoutMs);
}

// ---- wire protocol: request building + response parsing --------------------

std::string RequestEnum(const std::vector<std::wstring>& paths, bool hideShell) {
    std::string req = "ENUM";
    req.push_back(kFieldSepA);
    req += hideShell ? "1" : "0";
    for (const auto& p : paths) {
        req.push_back(kFieldSepA);
        req += WideToUtf8(p);
    }
    return req;
}

std::string RequestInvoke(UINT offset, const std::wstring& verb,
                          const std::wstring& title,
                          const std::vector<std::wstring>& paths) {
    std::string req = "INVOKE";
    req.push_back(kFieldSepA);
    req += std::to_string(offset);
    req.push_back(kFieldSepA);
    req += WideToUtf8(verb);
    req.push_back(kFieldSepA);
    req += WideToUtf8(title);
    for (const auto& p : paths) {
        req.push_back(kFieldSepA);
        req += WideToUtf8(p);
    }
    return req;
}

struct FlatRec {
    int depth = 0;
    LegacyMenuItem item;
};

void BuildLevel(std::vector<LegacyMenuItem>& out, std::vector<FlatRec>& recs,
                size_t& i, int level) {
    while (i < recs.size() && recs[i].depth == level) {
        LegacyMenuItem node = std::move(recs[i].item);
        ++i;
        if (i < recs.size() && recs[i].depth > level)
            BuildLevel(node.children, recs, i, level + 1);
        out.push_back(std::move(node));
    }
}

bool ParseEnumResponse(const std::string& bytes, std::vector<LegacyMenuItem>& out) {
    out.clear();
    const std::wstring w = Utf8ToWide(bytes);
    if (w.empty()) return false;

    size_t nl = w.find(L'\n');
    std::wstring header = (nl == std::wstring::npos) ? w : w.substr(0, nl);
    if (header.rfind(L"OK", 0) != 0) {
        Log(L"enum response was not OK: %s", header.c_str());
        return false;
    }

    std::vector<FlatRec> recs;
    size_t pos = (nl == std::wstring::npos) ? w.size() : nl + 1;
    while (pos < w.size()) {
        size_t eol = w.find(L'\n', pos);
        std::wstring line = (eol == std::wstring::npos) ? w.substr(pos)
                                                        : w.substr(pos, eol - pos);
        pos = (eol == std::wstring::npos) ? w.size() : eol + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;

        std::vector<std::wstring> f;
        size_t start = 0;
        while (f.size() < 5) {
            size_t sep = line.find(kFieldSep, start);
            if (sep == std::wstring::npos) break;
            f.push_back(line.substr(start, sep - start));
            start = sep + 1;
        }
        f.push_back(line.substr(start)); // title is always the remainder
        if (f.size() < 6) continue;

        FlatRec rec;
        rec.depth = _wtoi(f[0].c_str());
        rec.item.offset = (UINT)wcstoul(f[1].c_str(), nullptr, 10);
        rec.item.verb = f[2];
        rec.item.icon = f[3];
        rec.item.iconSource = f[4];
        rec.item.title = f[5];
        recs.push_back(std::move(rec));
    }

    size_t i = 0;
    BuildLevel(out, recs, i, 0);
    return true;
}

// ---- caching + public entry points ----------------------------------------

std::mutex g_cacheMutex;

struct CacheEntry {
    std::vector<LegacyMenuItem> tree;
    ULONGLONG builtAt = 0;   // GetTickCount64() when this entry was published
};

std::map<std::wstring, CacheEntry> g_cache;
constexpr size_t kMaxCache = 24;

// Default 10 minutes; override with LEGACYBRIDGE_CACHE_TTL_MS (>= 1000 ms).
ULONGLONG CacheTtlMs() {
    static ULONGLONG ttls = 0;
    if (ttls == 0) {
        ttls = 10ull * 60ull * 1000ull;
        wchar_t v[32] = { 0 };
        const DWORD n = GetEnvironmentVariableW(L"LEGACYBRIDGE_CACHE_TTL_MS", v, _countof(v));
        if (n > 0) {
            const ULONGLONG x = _wcstoui64(v, nullptr, 10);
            if (x >= 1000) ttls = x;
        }
    }
    return ttls;
}

// Caches by FILE TYPE (extension) so one .pdf warms every .pdf; non-files get a
// folder/drive/background key. Multi-selection is exact-path because the merged
// handler's result depends on the whole set. Per-file handlers (OneDrive, Git
// status) may therefore show briefly stale entries; stale-while-revalidate below
// refreshes them in the background on hit.
std::wstring FileTypeKey(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (path.size() == 3 && path[1] == L':' && path[2] == L'\\') return L"drive";
        return L"folder";
    }
    if (path.empty()) return L"background";
    const size_t dot = path.find_last_of(L'.');
    const size_t slash = path.find_last_of(L"\\/");
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        std::wstring ext = path.substr(dot);
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(static_cast<wint_t>(c)));
        return ext;
    }
    return L"noext";
}

std::wstring CacheKey(const std::vector<std::wstring>& paths) {
    if (paths.size() == 1) return FileTypeKey(paths[0]);
    std::wstring key = L"multi";
    for (const auto& p : paths) {
        key.push_back(kFieldSep);
        key += p;
    }
    return key;
}

// Show/hide the shell's own duplicate verbs. Default is to hide; set the
// env var LEGACYBRIDGE_SHOW_SHELL=1 to show everything again.
bool HideShellDefault() {
    wchar_t v[8] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"LEGACYBRIDGE_SHOW_SHELL", v, _countof(v));
    return !(n > 0 && v[0] == L'1');
}

bool RunEnumeration(const std::vector<std::wstring>& paths, bool hideShell,
                    std::vector<LegacyMenuItem>& out) {
    const std::string req = RequestEnum(paths, hideShell);
    std::string resp;

    if (Transact(req, resp, 8000) && ParseEnumResponse(resp, out) && !out.empty())
        return true;

    // One retry with a longer budget: the first (cold) build warms the legacy
    // handler DLLs, so the retry is normally fast and reliable.
    resp.clear();
    out.clear();
    if (Transact(req, resp, 15000) && ParseEnumResponse(resp, out))
        return true;

    return false;
}

// ---- asynchronous snapshot building ---------------------------------------
// Building the legacy menu (worker spawn + cold legacy-handler DLL load +
// QueryContextMenu + icon resolution) can take well over a second. Doing that
// inside EnumSubCommands would block the shell's menu thread and hold up every
// other modern extension, so it runs on a detached background thread here and
// callers never block on it (they serve from cache, or show "Loading...").

struct PendingBuild {
    std::mutex m;
    bool started = false;
};

std::mutex g_pendingMutex;
std::map<std::wstring, std::shared_ptr<PendingBuild>> g_pending;

void RunAsyncBuild(const std::vector<std::wstring>& paths, const std::wstring& key,
                   const std::shared_ptr<PendingBuild>& pb) {
    ScopedTimer timer(L"worker: async build");
    std::vector<LegacyMenuItem> tree;
    const bool ok = RunEnumeration(paths, HideShellDefault(), tree);

    if (ok && !tree.empty()) {
        CacheEntry entry;
        entry.tree = std::move(tree);
        entry.builtAt = GetTickCount64();
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        if (g_cache.size() >= kMaxCache && g_cache.find(key) == g_cache.end())
            g_cache.erase(g_cache.begin());
        g_cache[key] = std::move(entry);
    }

    std::lock_guard<std::mutex> lk(g_pendingMutex);
    g_pending.erase(key);
}

std::shared_ptr<PendingBuild> PendingFor(const std::wstring& key) {
    std::lock_guard<std::mutex> lk(g_pendingMutex);
    auto it = g_pending.find(key);
    if (it != g_pending.end()) return it->second;
    auto pb = std::make_shared<PendingBuild>();
    g_pending[key] = pb;
    return pb;
}

void EnsureBuild(const std::vector<std::wstring>& paths, const std::wstring& key,
                 const std::shared_ptr<PendingBuild>& pb) {
    {
        std::lock_guard<std::mutex> lk(pb->m);
        if (pb->started) return;
        pb->started = true;
    }
    // Detached: the surrogate never unloads the DLL (DllCanUnloadNow returns
    // S_FALSE) and process teardown is the only reason these threads end.
    std::thread([paths, key, pb]() { RunAsyncBuild(paths, key, pb); }).detach();
}

void StartSnapshotWorkerPrewarm() {
    static std::atomic<bool> prewarmed{false};
    if (prewarmed.exchange(true)) return;
    // Spawn the warm worker process off-thread so the first right-click doesn't
    // pay the CreateProcess + handler-DLL load cost. DllMain can't do this
    // (loader lock), so it is triggered from the first class-factory call.
    std::thread([]() {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        g_worker.EnsureStarted();
    }).detach();
}

} // namespace

void StartSnapshotWarmup(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;

    const std::wstring key = CacheKey(paths);
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        auto it = g_cache.find(key);
        if (it != g_cache.end() &&
            (GetTickCount64() - it->second.builtAt) <= CacheTtlMs())
            return;   // fresh; nothing to do
    }

    const auto pb = PendingFor(key);
    EnsureBuild(paths, key, pb);
}

bool TryGetSnapshot(const std::vector<std::wstring>& paths,
                    std::vector<LegacyMenuItem>& out) {
    out.clear();
    if (paths.empty()) return true;

    const std::wstring key = CacheKey(paths);
    bool hit = false;
    bool expired = false;
    {
        std::lock_guard<std::mutex> lk(g_cacheMutex);
        auto it = g_cache.find(key);
        if (it != g_cache.end()) {
            hit = true;
            out = it->second.tree;
            expired = (GetTickCount64() - it->second.builtAt) > CacheTtlMs();
        }
    }

    if (hit) {
        TimingLog(expired ? L"cache hit (stale, revalidating): %s" : L"cache hit: %s",
                  paths[0].c_str());
        if (expired) {
            const auto pb = PendingFor(key);
            EnsureBuild(paths, key, pb);   // stale-while-revalidate
        }
        return true;
    }

    TimingLog(L"cache miss: %s", paths[0].c_str());
    const auto pb = PendingFor(key);
    EnsureBuild(paths, key, pb);
    return false;   // cold: return immediately, never block the menu thread
}

bool IsSnapshotReady(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return true;

    const std::wstring key = CacheKey(paths);
    std::lock_guard<std::mutex> lk(g_cacheMutex);
    return g_cache.find(key) != g_cache.end();
}

HRESULT InvokeLegacyItem(const std::vector<std::wstring>& paths, UINT offset,
                         const std::wstring& verb, const std::wstring& title) {
    if (paths.empty()) return E_INVALIDARG;

    const std::string req = RequestInvoke(offset, verb, title, paths);
    std::string resp;
    if (!Transact(req, resp, 20000)) {
        Log(L"InvokeLegacyItem: worker transaction failed");
        return E_FAIL;
    }

    const std::wstring result = Utf8ToWide(resp);
    if (result.rfind(L"OK", 0) == 0) return S_OK;
    if (result.rfind(L"NOTFOUND", 0) == 0) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    Log(L"InvokeLegacyItem: worker returned %s", result.c_str());
    return E_FAIL;
}

void ShutdownSnapshotWorker() {
    std::lock_guard<std::mutex> lock(g_workerMutex);
    g_worker.Stop(true);
}

} // namespace legacy
