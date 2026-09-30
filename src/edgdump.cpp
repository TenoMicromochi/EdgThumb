// Test harness: edgdump <in.edg> <out.png>
// Runs the same decoder the shell extension uses, so the output proves the
// DLL's colours, layer order and transparency without going through Explorer.
// Exit code: 0 = written, 1 = failed, 2 = bad usage.

#include <windows.h>
#include <objbase.h>          // CoTaskMemFree
#include <stdio.h>
#include <vector>
#include "edg.h"
#include "../third_party/miniz/miniz.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: edgdump <in.edg> <out.png>\n"); return 2; }

    HANDLE f = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { wprintf(L"cannot open input\n"); return 1; }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(f, &fileSize)) {
        CloseHandle(f);
        wprintf(L"cannot get input size\n");
        return 1;
    }
    if (fileSize.QuadPart == 0 || fileSize.QuadPart > (LONGLONG)kEdgMaxBytes) {
        CloseHandle(f);
        wprintf(L"input is empty or larger than 1 GiB\n");
        return 1;
    }

    DWORD size = (DWORD)fileSize.QuadPart;
    std::vector<BYTE> buf;
    try {
        buf.resize(size);
    } catch (...) {
        CloseHandle(f);
        wprintf(L"out of memory\n");
        return 1;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(f, buf.data(), size, &got, nullptr);
    CloseHandle(f);
    if (!ok || got != size) { wprintf(L"cannot read input\n"); return 1; }

    UINT w = 0, h = 0;
    BYTE* px = nullptr;
    if (FAILED(EdgDecodeCurrentPage(buf.data(), got, &w, &h, &px))) {
        wprintf(L"decode failed\n");
        return 1;
    }

    // The decoder hands back BGRA; PNG wants RGBA.
    for (size_t i = 0; i < (size_t)w * h; i++) {
        BYTE t = px[i * 4 + 0];
        px[i * 4 + 0] = px[i * 4 + 2];
        px[i * 4 + 2] = t;
    }

    size_t pngLen = 0;
    void* png = tdefl_write_image_to_png_file_in_memory_ex(px, w, h, 4, &pngLen,
                                                           6, MZ_FALSE);
    CoTaskMemFree(px);
    if (!png) { wprintf(L"png encode failed\n"); return 1; }

    FILE* o = nullptr;
    _wfopen_s(&o, argv[2], L"wb");
    if (!o) { mz_free(png); wprintf(L"cannot open output\n"); return 1; }
    size_t wrote = fwrite(png, 1, pngLen, o);
    fclose(o);
    mz_free(png);

    if (wrote != pngLen) { wprintf(L"png write failed\n"); return 1; }
    wprintf(L"%ux%u -> %s\n", w, h, argv[2]);
    return 0;
}
