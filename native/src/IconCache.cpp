#include "Common.h"
#include "IconCache.h"
#include "Log.h"

#include <KnownFolders.h>
#include <ShlObj.h>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <stdint.h>
#include <vector>

namespace legacy {
namespace {

// Menu "bitmap" sentinels are not real HBITMAPs and must never be treated as one.
bool IsUsableHBitmap(HBITMAP b) {
    if (!b) return false;
    return b != HBMMENU_CALLBACK && b != HBMMENU_SYSTEM &&
           b != HBMMENU_MBAR_RESTORE && b != HBMMENU_MBAR_MINIMIZE &&
           b != HBMMENU_MBAR_CLOSE && b != HBMMENU_MBAR_CLOSE_D &&
           b != HBMMENU_MBAR_MINIMIZE_D && b != HBMMENU_POPUP_CLOSE &&
           b != HBMMENU_POPUP_RESTORE && b != HBMMENU_POPUP_MINIMIZE &&
           b != HBMMENU_POPUP_MAXIMIZE;
}

// Reads an HBITMAP into a top-down 32bpp BGRA buffer (row 0 == top scanline).
bool HBitmapTo32bpp(HBITMAP hbmp, LONG& w, LONG& h, std::vector<unsigned char>& bgra) {
    BITMAP bm = {};
    if (!GetObjectW(hbmp, sizeof(bm), &bm)) return false;
    w = bm.bmWidth;
    h = bm.bmHeight;
    if (w <= 0 || h <= 0) return false;

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h;   // top-down
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    bgra.resize((size_t)w * (size_t)h * 4);

    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    const int got = GetDIBits(dc, hbmp, 0, (UINT)h, bgra.data(),
                              reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (got != (int)h) return false;

    // GDI fills the alpha byte with 0 for sources that carry no alpha channel
    // (e.g. a 24bpp DDB). If every pixel is alpha 0, assume the icon is opaque.
    bool anyAlpha = false;
    for (size_t i = 3; i < bgra.size(); i += 4) {
        if (bgra[i] != 0) { anyAlpha = true; break; }
    }
    if (!anyAlpha) {
        for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
    }
    return true;
}

// Bilinear resample of a top-down 32bpp BGRA buffer to (tw,th), blending on
// premultiplied color so semi-transparent pixels don't get dark/light halos.
bool Resample(const std::vector<unsigned char>& src, LONG sw, LONG sh,
              LONG tw, LONG th, std::vector<unsigned char>& dst) {
    if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0) return false;
    dst.assign((size_t)tw * (size_t)th * 4, 0);

    auto clamp = [](LONG v, LONG max) { return (v < 0) ? 0 : ((v >= max) ? (max - 1) : v); };

    for (LONG y = 0; y < th; ++y) {
        const double sy = (y + 0.5) * (double)sh / th - 0.5;
        LONG y0 = (LONG)std::floor(sy);
        const double fy = sy - y0;
        y0 = clamp(y0, sh);
        const LONG y1 = clamp(y0 + 1, sh);

        for (LONG x = 0; x < tw; ++x) {
            const double sx = (x + 0.5) * (double)sw / tw - 0.5;
            LONG x0 = (LONG)std::floor(sx);
            const double fx = sx - x0;
            x0 = clamp(x0, sw);
            const LONG x1 = clamp(x0 + 1, sw);

            const LONG xs[2] = { x0, x1 };
            const LONG ys[2] = { y0, y1 };

            double p[4][4];
            for (int k = 0; k < 4; ++k) {
                const unsigned char* s = &src[((size_t)ys[k >> 1] * sw + xs[k & 1]) * 4];
                const double a = s[3] / 255.0;
                p[k][0] = s[0] * a;
                p[k][1] = s[1] * a;
                p[k][2] = s[2] * a;
                p[k][3] = a;
            }

            const double w00 = (1 - fx) * (1 - fy);
            const double w10 = fx * (1 - fy);
            const double w01 = (1 - fx) * fy;
            const double w11 = fx * fy;

            const double b = p[0][0] * w00 + p[1][0] * w10 + p[2][0] * w01 + p[3][0] * w11;
            const double g = p[0][1] * w00 + p[1][1] * w10 + p[2][1] * w01 + p[3][1] * w11;
            const double r = p[0][2] * w00 + p[1][2] * w10 + p[2][2] * w01 + p[3][2] * w11;
            const double a = p[0][3] * w00 + p[1][3] * w10 + p[2][3] * w01 + p[3][3] * w11;

            unsigned char* o = &dst[((size_t)y * tw + x) * 4];
            if (a > 0.0) {
                o[0] = (unsigned char)(b / a + 0.5);
                o[1] = (unsigned char)(g / a + 0.5);
                o[2] = (unsigned char)(r / a + 0.5);
            }
            o[3] = (unsigned char)(a * 255.0 + 0.5);
        }
    }
    return true;
}

void Append16(std::vector<unsigned char>& out, uint16_t v) {
    out.push_back((unsigned char)(v & 0xFF));
    out.push_back((unsigned char)((v >> 8) & 0xFF));
}

void Append32(std::vector<unsigned char>& out, uint32_t v) {
    out.push_back((unsigned char)(v & 0xFF));
    out.push_back((unsigned char)((v >> 8) & 0xFF));
    out.push_back((unsigned char)((v >> 16) & 0xFF));
    out.push_back((unsigned char)((v >> 24) & 0xFF));
}

void Put16(std::vector<unsigned char>& out, size_t at, uint16_t v) {
    out[at] = (unsigned char)(v & 0xFF);
    out[at + 1] = (unsigned char)((v >> 8) & 0xFF);
}

void Put32(std::vector<unsigned char>& out, size_t at, uint32_t v) {
    out[at] = (unsigned char)(v & 0xFF);
    out[at + 1] = (unsigned char)((v >> 8) & 0xFF);
    out[at + 2] = (unsigned char)((v >> 16) & 0xFF);
    out[at + 3] = (unsigned char)((v >> 24) & 0xFF);
}

// Builds a two-frame (16px + 32px) .ico in memory. Frames are top-down 32bpp
// BGRA; the AND mask is left zero so the alpha channel drives transparency.
bool BuildIco(const std::vector<unsigned char>& bgra16,
              const std::vector<unsigned char>& bgra32,
              std::vector<unsigned char>& out) {
    out.clear();
    Append16(out, 0);   // reserved
    Append16(out, 1);   // type: icon
    Append16(out, 2);   // count

    const size_t header = 6;
    const size_t entriesEnd = header + 2 * 16;
    out.resize(entriesEnd);

    const int sizes[2] = { 16, 32 };
    const std::vector<unsigned char>* frames[2] = { &bgra16, &bgra32 };

    size_t offset = entriesEnd;
    for (int i = 0; i < 2; ++i) {
        const int s = sizes[i];
        const std::vector<unsigned char>& px = *frames[i];
        const uint32_t xorBytes = (uint32_t)s * (uint32_t)s * 4;
        const uint32_t andStride = (((uint32_t)s + 31) / 32) * 4;
        const uint32_t andBytes = andStride * (uint32_t)s;
        const uint32_t bytesInRes = 40 + xorBytes + andBytes;

        const size_t e = header + (size_t)i * 16;
        out[e] = (s >= 256) ? 0 : (unsigned char)s;      // width
        out[e + 1] = (s >= 256) ? 0 : (unsigned char)s;  // height
        out[e + 2] = 0;                                   // color count
        out[e + 3] = 0;                                   // reserved
        Put16(out, e + 4, 1);                             // planes
        Put16(out, e + 6, 32);                            // bit count
        Put32(out, e + 8, bytesInRes);                    // bytes in resource
        Put32(out, e + 12, (uint32_t)offset);             // image offset

        Append32(out, 40);            // biSize
        Append32(out, (uint32_t)s);   // biWidth
        Append32(out, (uint32_t)(s * 2)); // biHeight (XOR + AND)
        Append16(out, 1);             // biPlanes
        Append16(out, 32);            // biBitCount
        Append32(out, 0);             // biCompression (BI_RGB)
        Append32(out, xorBytes + andBytes); // biSizeImage
        Append32(out, 0);             // biXPelsPerMeter
        Append32(out, 0);             // biYPelsPerMeter
        Append32(out, 0);             // biClrUsed
        Append32(out, 0);             // biClrImportant

        for (int row = s - 1; row >= 0; --row)
            out.insert(out.end(), px.begin() + (size_t)row * s * 4,
                       px.begin() + (size_t)(row + 1) * s * 4);

        out.insert(out.end(), andBytes, 0);

        offset += bytesInRes;
    }
    return true;
}

uint64_t Fnv1a(const std::wstring& key) {
    uint64_t h = 14695981039346656037ull;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(key.data());
    const unsigned char* e = p + key.size() * sizeof(wchar_t);
    for (; p < e; ++p) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return h;
}

std::wstring LocalAppDataPath() {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                       nullptr, &path)) && path) {
        std::wstring s(path);
        CoTaskMemFree(path);
        return s;
    }
    return L"";
}

bool FileEquals(const std::wstring& path, const std::vector<unsigned char>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size != (DWORD)data.size()) {
        CloseHandle(h);
        return false;
    }
    std::vector<unsigned char> buf(size);
    DWORD read = 0;
    const bool ok = ReadFile(h, buf.data(), size, &read, nullptr) && read == size;
    CloseHandle(h);
    return ok && memcmp(buf.data(), data.data(), size) == 0;
}

bool WriteAllBytes(const std::wstring& path, const std::vector<unsigned char>& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) &&
                    written == data.size();
    CloseHandle(h);
    return ok;
}

std::wstring QuoteIfNeeded(const std::wstring& p) {
    if (p.find(L' ') == std::wstring::npos) return p;
    return L"\"" + p + L"\"";
}

} // namespace

std::wstring CacheBitmapIcon(HBITMAP hbmp, const std::wstring& key) {
    if (!IsUsableHBitmap(hbmp)) return L"";

    LONG w = 0, h = 0;
    std::vector<unsigned char> src;
    if (!HBitmapTo32bpp(hbmp, w, h, src)) return L"";

    std::vector<unsigned char> f16, f32;
    if (!Resample(src, w, h, 16, 16, f16)) return L"";
    if (!Resample(src, w, h, 32, 32, f32)) return L"";

    std::vector<unsigned char> ico;
    if (!BuildIco(f16, f32, ico)) return L"";

    const std::wstring base = LocalAppDataPath();
    if (base.empty()) return L"";
    const std::wstring dir = base + L"\\bres-pos\\icons";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) return L"";

    wchar_t hexBuf[17] = { 0 };
    swprintf_s(hexBuf, L"%016llx", (unsigned long long)Fnv1a(key));
    const std::wstring path = dir + L"\\" + hexBuf + L".ico";

    if (FileEquals(path, ico)) return QuoteIfNeeded(path);   // reuse unchanged
    if (!WriteAllBytes(path, ico)) return L"";
    return QuoteIfNeeded(path);
}

} // namespace legacy
