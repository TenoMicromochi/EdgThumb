#include "edg.h"
#include <objbase.h>          // CoTaskMemAlloc
#include <string.h>
#include <memory>
#include <new>
#include <vector>
#include "../third_party/miniz/miniz.h"

// The layout of an .edg file follows the EDGE2 File Loader Library by Takabo
// Soft (MIT). No code from it is included; README.md describes what is used.

namespace {

// The body of an .edg file is a flat sequence of chunks:
//   id (uint16 LE) | length (uint32 LE) | data[length]
// Containers (pages, layers, palettes, ...) hold the same thing again. A chunk
// id is only unique inside its own level.
struct Span { const BYTE* p; size_t n; };

struct Chunk { WORD id; Span val; };

// Reads the chunk starting at *off. Returns false at the end of the buffer or
// on a length that would run past it (the rest of that level is dropped).
bool NextChunk(Span s, size_t* off, Chunk* out)
{
    if (s.n - *off < 6) return false;             // *off never exceeds s.n
    const BYTE* h = s.p + *off;
    WORD id   = (WORD)(h[0] | (h[1] << 8));
    DWORD len = (DWORD)h[2] | ((DWORD)h[3] << 8) | ((DWORD)h[4] << 16) |
                ((DWORD)h[5] << 24);
    if ((size_t)len > s.n - *off - 6) return false;
    out->id = id;
    out->val.p = h + 6;
    out->val.n = len;
    *off += 6 + (size_t)len;
    return true;
}

// Fixed-width little-endian scalars. A chunk shorter than the field is ignored.
bool GetU8(Span s, BYTE* v)
{
    if (s.n < 1) return false;
    *v = s.p[0];
    return true;
}
bool GetU16(Span s, DWORD* v)
{
    if (s.n < 2) return false;
    *v = (DWORD)(s.p[0] | (s.p[1] << 8));
    return true;
}
bool GetU32(Span s, DWORD* v)
{
    if (s.n < 4) return false;
    *v = (DWORD)s.p[0] | ((DWORD)s.p[1] << 8) | ((DWORD)s.p[2] << 16) |
         ((DWORD)s.p[3] << 24);
    return true;
}
bool GetI32(Span s, int* v)
{
    DWORD u;
    if (!GetU32(s, &u)) return false;
    *v = (int)u;
    return true;
}

// ------------------------------------------------------------ file model ---

struct BackColor { BYTE index = 0; BYTE b = 0, g = 0, r = 0; };

struct PaletteInfo {
    int  id = 0;
    int  colors = 0;
    Span rgb{ nullptr, 0 };        // BGR triplets; p == nullptr if not valid
};

struct PageRef { Span body; int bits; };

struct Root {
    int  bits = 0;
    bool fixedPalette = false;
    int  curPage = 0;
    int  colorCount = 0;
    int  curPalette = 0;
    BackColor back;
    std::vector<PageRef> pages;
    std::vector<PaletteInfo> palettes;
};

struct LayerInfo {
    bool grouped = false;
    bool visible = true;
    Span image{ nullptr, 0 };      // p == nullptr if absent or the wrong length
};

struct PageInfo {
    size_t w = 0, h = 0;
    int    paletteId = 0, paletteCache = 0;
    BYTE   mode = 0;
    int    curLayer = 0;
    std::vector<LayerInfo> layers;   // index 0 is the topmost layer
};

int BytesPerDot(int bits)
{
    switch (bits) {
    case 1: case 4: case 8:            return 1;
    case 15: case 16: case 24:         return 3;
    default:                           return 0;
    }
}

void ReadBackColor(Span s, BackColor* bc)
{
    size_t off = 0; Chunk c;
    while (NextChunk(s, &off, &c)) {
        BYTE v;
        if (c.id == 1000 && GetU8(c.val, &v)) bc->index = v;
        if (c.id == 1001 && c.val.n >= 3) {
            bc->b = c.val.p[0]; bc->g = c.val.p[1]; bc->r = c.val.p[2];
        }
    }
}

// Chunks are handled in file order and the last one wins, as the official
// loader does. Chunks that depend on an earlier one (the palette on the colour
// count, layers on the page size) use the value in effect when they appear.
bool ReadPalette(Span s, int colors, PaletteInfo* pal)
{
    if (colors < 1 || colors > 256) return false;
    pal->colors = colors;
    size_t off = 0; Chunk c;
    while (NextChunk(s, &off, &c)) {
        int v;
        if (c.id == 1001 && GetI32(c.val, &v)) pal->id = v;
        if (c.id == 1005 && c.val.n == (size_t)colors * 3) pal->rgb = c.val;
    }
    return true;
}

bool ReadRoot(Span body, Root* r)
{
    size_t off = 0; Chunk c;
    while (NextChunk(body, &off, &c)) {
        BYTE b8; DWORD u; int i;
        switch (c.id) {
        case 1000: if (GetU8(c.val, &b8)) r->bits = b8; break;
        case 2002: if (GetU8(c.val, &b8)) r->fixedPalette = (b8 != 0); break;
        case 2003: r->pages.push_back(PageRef{ c.val, r->bits }); break;
        case 2004: if (GetI32(c.val, &i)) r->curPage = i; break;
        case 3001: if (GetU16(c.val, &u)) r->colorCount = (int)u; break;
        case 3003: {
            PaletteInfo p;
            if (!ReadPalette(c.val, r->colorCount, &p)) return false;
            r->palettes.push_back(p);
            break;
        }
        case 3004: if (GetI32(c.val, &i)) r->curPalette = i; break;
        case 3006: if (GetU32(c.val, &u)) r->back.index = (BYTE)(u & 0xFF); break;
        case 3008: ReadBackColor(c.val, &r->back); break;
        }
    }
    return true;
}

void ReadLayer(Span s, size_t imageBytes, LayerInfo* l)
{
    size_t off = 0; Chunk c;
    while (NextChunk(s, &off, &c)) {
        BYTE b8;
        if (c.id == 1002 && GetU8(c.val, &b8)) l->grouped = (b8 != 0);
        if (c.id == 1005 && GetU8(c.val, &b8)) l->visible = (b8 != 0);
        if (c.id == 1006 && c.val.n == imageBytes) l->image = c.val;
    }
}

bool ReadPage(const PageRef& ref, PageInfo* pg)
{
    const size_t bpd = (size_t)BytesPerDot(ref.bits);
    if (bpd == 0) return false;

    size_t off = 0; Chunk c;
    while (NextChunk(ref.body, &off, &c)) {
        DWORD u; int i; BYTE b8;
        switch (c.id) {
        case 1005: if (GetU16(c.val, &u)) pg->w = u; break;
        case 1006: if (GetU16(c.val, &u)) pg->h = u; break;
        case 1007: if (GetI32(c.val, &i)) pg->paletteId = i; break;
        case 1008: if (GetI32(c.val, &i)) pg->paletteCache = i; break;
        case 2000: if (GetU8(c.val, &b8)) pg->mode = b8; break;
        case 2003: {
            LayerInfo l;
            ReadLayer(c.val, pg->w * pg->h * bpd, &l);
            pg->layers.push_back(l);
            break;
        }
        case 2004: if (GetI32(c.val, &i)) pg->curLayer = i; break;
        }
    }
    return pg->w != 0 && pg->h != 0;
}

// ------------------------------------------------------------ visibility ---

// The group that layer i belongs to is [start, end). Children have "grouped"
// set; the group's head is the first layer above them without it.
//
// The official Items::group_range() keeps lowering start for every grouped
// layer above i instead of stopping at the first ungrouped one, so in a file
// with two or more groups it can return the head of a different group. This
// follows the intended meaning instead.
void GroupRange(const std::vector<LayerInfo>& L, int i, int* start, int* end)
{
    int n = (int)L.size();
    int s = i;
    while (s > 0 && L[s].grouped) s--;
    int e = s + 1;
    while (e < n && L[e].grouped) e++;
    *start = s; *end = e;
}

// Same rules as the official Page::layer_visible().
bool LayerVisible(const PageInfo& pg, int i)
{
    const std::vector<LayerInfo>& L = pg.layers;
    const int cur = pg.curLayer;
    switch (pg.mode) {
    case 1: return i == cur;
    case 2: return i == cur || i == cur - 1;
    case 3: return i == cur || i == cur + 1;
    case 4: return i <= cur;
    case 5: return i >= cur;
    case 6: {
        if (cur < 0 || cur >= (int)L.size()) return false;
        int s, e;
        GroupRange(L, cur, &s, &e);
        return i >= s && i < e;
    }
    default: {                    // 0, and any mode this reader does not know
        if (!L[i].visible) return false;
        if (!L[i].grouped) return true;
        int s, e;
        GroupRange(L, i, &s, &e);
        return L[s].visible;
    }
    }
}

// -------------------------------------------------------------- composite ---

HRESULT Decode(const BYTE* data, size_t size,
               UINT* outW, UINT* outH, BYTE** outPixels)
{
    // Header: "EDGE2", u16 local version (0), u8 compression (1 = zlib),
    // u32 expanded size, then the zlib stream.
    const size_t kHeader = 12;
    if (size <= kHeader || size > kEdgMaxBytes) return E_FAIL;
    if (memcmp(data, "EDGE2", 5) != 0) return E_FAIL;
    if (data[5] != 0 || data[6] != 0) return E_FAIL;
    if (data[7] != 1) return E_FAIL;
    DWORD rawSize = (DWORD)data[8] | ((DWORD)data[9] << 8) |
                    ((DWORD)data[10] << 16) | ((DWORD)data[11] << 24);
    if (rawSize == 0 || rawSize > kEdgMaxBytes) return E_FAIL;

    // The size is stored, so inflate once into a buffer of exactly that size.
    std::unique_ptr<BYTE[]> body(new BYTE[rawSize]);
    mz_ulong got = rawSize;
    int rc = mz_uncompress(body.get(), &got, data + kHeader,
                           (mz_ulong)(size - kHeader));
    if (rc != MZ_OK || got != rawSize) return E_FAIL;

    Root root;
    if (!ReadRoot(Span{ body.get(), rawSize }, &root)) return E_FAIL;

    const int bpd = BytesPerDot(root.bits);
    if (bpd == 0) return E_FAIL;

    // The page EDGE2 has open. Out of range falls back to the first page.
    if (root.pages.empty()) return E_FAIL;
    size_t pageIndex = (root.curPage >= 0 && (size_t)root.curPage < root.pages.size())
                           ? (size_t)root.curPage : 0;
    PageInfo pg;
    if (!ReadPage(root.pages[pageIndex], &pg)) return E_FAIL;

    // Palette (indexed files only).
    const PaletteInfo* pal = nullptr;
    if (bpd == 1) {
        const std::vector<PaletteInfo>& P = root.palettes;
        if (root.fixedPalette) {
            if (pg.paletteCache >= 0 && (size_t)pg.paletteCache < P.size() &&
                P[pg.paletteCache].id == pg.paletteId)
                pal = &P[pg.paletteCache];
            for (size_t k = 0; !pal && k < P.size(); k++)
                if (P[k].id == pg.paletteId) pal = &P[k];
        }
        if (!pal) {
            if (P.empty()) return E_FAIL;
            size_t k = (root.curPalette >= 0 && (size_t)root.curPalette < P.size())
                           ? (size_t)root.curPalette : 0;
            pal = &P[k];
        }
        if (!pal->rgb.p) return E_FAIL;
    }

    // Composite into one buffer: fill with the background colour, then draw
    // the visible layers from the bottom one up. Dots equal to the background
    // colour are holes and are not drawn. The layer images are read in place.
    const size_t count = pg.w * pg.h;
    const size_t bytes = count * (size_t)bpd;
    std::unique_ptr<BYTE[]> comp(new BYTE[bytes]);
    const BackColor& bg = root.back;
    if (bpd == 1) {
        memset(comp.get(), bg.index, bytes);
    } else {
        for (size_t k = 0; k < count; k++) {
            comp[k * 3 + 0] = bg.b;
            comp[k * 3 + 1] = bg.g;
            comp[k * 3 + 2] = bg.r;
        }
    }

    for (int i = (int)pg.layers.size() - 1; i >= 0; i--) {
        if (!LayerVisible(pg, i)) continue;
        const LayerInfo& l = pg.layers[i];
        if (!l.image.p) {
            // The official loader keeps such a layer as an all-zero image.
            if (bpd == 1) {
                if (bg.index != 0) memset(comp.get(), 0, bytes);
            } else if (bg.b || bg.g || bg.r) {
                memset(comp.get(), 0, bytes);
            }
            continue;
        }
        const BYTE* src = l.image.p;
        BYTE* dst = comp.get();
        if (bpd == 1) {
            for (size_t k = 0; k < count; k++)
                if (src[k] != bg.index) dst[k] = src[k];
        } else {
            for (size_t k = 0; k < count; k++) {
                const BYTE* s3 = src + k * 3;
                if (s3[0] != bg.b || s3[1] != bg.g || s3[2] != bg.r) {
                    dst[k * 3 + 0] = s3[0];
                    dst[k * 3 + 1] = s3[1];
                    dst[k * 3 + 2] = s3[2];
                }
            }
        }
    }

    // Colour conversion to BGRA. Background dots become fully transparent.
    BYTE* out = (BYTE*)CoTaskMemAlloc(count * 4);
    if (!out) return E_OUTOFMEMORY;
    DWORD* px = (DWORD*)out;

    if (bpd == 1) {
        DWORD lut[256];
        for (int c = 0; c < 256; c++) {
            if (c == bg.index) {
                lut[c] = 0;
            } else if (c < pal->colors) {
                const BYTE* e = pal->rgb.p + (size_t)c * 3;
                lut[c] = 0xFF000000u | ((DWORD)e[2] << 16) | ((DWORD)e[1] << 8) | e[0];
            } else {
                lut[c] = 0xFF000000u;
            }
        }
        for (size_t k = 0; k < count; k++) px[k] = lut[comp[k]];
    } else {
        for (size_t k = 0; k < count; k++) {
            const BYTE* s3 = comp.get() + k * 3;
            if (s3[0] == bg.b && s3[1] == bg.g && s3[2] == bg.r)
                px[k] = 0;
            else
                px[k] = 0xFF000000u | ((DWORD)s3[2] << 16) | ((DWORD)s3[1] << 8) | s3[0];
        }
    }

    *outW = (UINT)pg.w; *outH = (UINT)pg.h; *outPixels = out;
    return S_OK;
}

} // namespace

HRESULT EdgDecodeCurrentPage(const BYTE* data, size_t size,
                             UINT* outW, UINT* outH, BYTE** outPixels)
{
    *outW = *outH = 0;
    *outPixels = nullptr;
    if (!data) return E_FAIL;
    try {
        return Decode(data, size, outW, outH, outPixels);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}
