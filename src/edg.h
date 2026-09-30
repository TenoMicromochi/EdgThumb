// EDGE2 (.edg) decoder - decodes the current page of an .edg file into 32bpp
// BGRA, laid out the way EDGE2 itself shows it (visibility modes and groups
// included). README.md has the format notes.
#pragma once
#include <windows.h>

// Largest input file and largest expanded body the decoder accepts.
const size_t kEdgMaxBytes = (size_t)1 << 30;

// Decodes the current page of an .edg file.
//   data/size : whole file contents
//   outW/outH : page size in pixels
//   outPixels : receives w*h BGRA quads (CoTaskMemAlloc'd, caller frees).
//               Transparent pixels are set to 0x00000000 so that the result is
//               valid both as straight and as premultiplied ARGB.
// Returns S_OK, E_OUTOFMEMORY, or E_FAIL for anything malformed. Never throws.
HRESULT EdgDecodeCurrentPage(const BYTE* data, size_t size,
                             UINT* outW, UINT* outH, BYTE** outPixels);
