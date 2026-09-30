// Explorer thumbnail handler for EDGE2 (.edg) pixel-art files.
//
// An IThumbnailProvider that shows the page EDGE2 has open. Registration is
// done by the installer (installer\EdgThumb.iss), not by this DLL. Scaling is
// integer-only and never interpolates: .edg files are pixel art and any
// smoothing destroys them.

#include <windows.h>
#include <thumbcache.h>
#include <new>
#include "edg.h"

// {34907B7E-9EC3-4494-8D6B-D7EBD3E19F64}
static const CLSID CLSID_EdgThumbProvider =
    { 0x34907b7e, 0x9ec3, 0x4494, { 0x8d, 0x6b, 0xd7, 0xeb, 0xd3, 0xe1, 0x9f, 0x64 } };

static LONG g_refs = 0;

// ---------------------------------------------------------------- scaling ---

// Picks an integer scale for fitting w x h into a cx x cx box.
// Returns a numerator/denominator pair where exactly one of them is 1.
static void PickIntegerScale(UINT w, UINT h, UINT cx, UINT* mul, UINT* div)
{
    UINT longest = (w > h) ? w : h;
    if (longest == 0) { *mul = 1; *div = 1; return; }

    if (longest <= cx) {
        // Small art: magnify by a whole number of pixels, never a fraction.
        *mul = cx / longest;
        if (*mul < 1) *mul = 1;
        *div = 1;
    } else {
        // Large art: keep only every Nth pixel. Averaging would blur the dots.
        *mul = 1;
        *div = (longest + cx - 1) / cx;
    }
}

// Nearest-neighbour resample into a fresh top-down 32bpp DIB.
//
// The result is always exactly cx by cx, with the artwork centred on a
// transparent canvas. Returning the scaled artwork on its own is not enough:
// the shell stretches an undersized bitmap up to the size it asked for, with
// smoothing, which is exactly what must not happen to pixel art. Handing back
// the requested size leaves the shell nothing to resample.
static HBITMAP MakeScaledBitmap(const BYTE* src, UINT w, UINT h, UINT cx)
{
    UINT mul = 1, div = 1;
    PickIntegerScale(w, h, cx, &mul, &div);

    UINT dw = (UINT)(((ULONGLONG)w * mul) / div);
    UINT dh = (UINT)(((ULONGLONG)h * mul) / div);
    if (dw == 0) dw = 1;
    if (dh == 0) dh = 1;
    if (dw > cx) dw = cx;
    if (dh > cx) dh = cx;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = (LONG)cx;
    bi.bmiHeader.biHeight      = -(LONG)cx;      // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BYTE* dst = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&dst,
                                   nullptr, 0);
    if (!bmp) return nullptr;

    ZeroMemory(dst, (size_t)cx * cx * 4);        // transparent margins

    UINT offX = (cx - dw) / 2;
    UINT offY = (cx - dh) / 2;

    for (UINT y = 0; y < dh; y++) {
        UINT sy = (UINT)(((ULONGLONG)y * div) / mul);
        if (sy >= h) sy = h - 1;
        const DWORD* srow = (const DWORD*)(src + (size_t)sy * w * 4);
        DWORD* drow = (DWORD*)(dst + ((size_t)(y + offY) * cx + offX) * 4);
        for (UINT x = 0; x < dw; x++) {
            UINT sx = (UINT)(((ULONGLONG)x * div) / mul);
            if (sx >= w) sx = w - 1;
            drow[x] = srow[sx];
        }
    }
    return bmp;
}

// --------------------------------------------------------------- provider ---

class CEdgThumbProvider : public IInitializeWithStream, public IThumbnailProvider
{
public:
    CEdgThumbProvider() : m_refs(1), m_stream(nullptr) { InterlockedIncrement(&g_refs); }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IInitializeWithStream)
            *ppv = static_cast<IInitializeWithStream*>(this);
        else if (riid == IID_IThumbnailProvider)
            *ppv = static_cast<IThumbnailProvider*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refs); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_refs);
        if (n == 0) delete this;
        return n;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* stream, DWORD)
    {
        if (!stream) return E_INVALIDARG;
        if (m_stream) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        return stream->QueryInterface(IID_PPV_ARGS(&m_stream));
    }

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
    {
        if (!phbmp || !pdwAlpha) return E_POINTER;
        *phbmp = nullptr;
        *pdwAlpha = WTSAT_ARGB;
        try {
            if (!m_stream) return E_UNEXPECTED;
            if (cx == 0) cx = 96;

            BYTE* file = nullptr;
            size_t size = 0;
            HRESULT hr = ReadWholeStream(&file, &size);
            if (FAILED(hr)) return hr;

            UINT w = 0, h = 0;
            BYTE* pixels = nullptr;
            hr = EdgDecodeCurrentPage(file, size, &w, &h, &pixels);
            delete[] file;
            if (FAILED(hr)) return hr;

            HBITMAP bmp = MakeScaledBitmap(pixels, w, h, cx);
            CoTaskMemFree(pixels);
            if (!bmp) return E_OUTOFMEMORY;

            *phbmp = bmp;
            return S_OK;
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_FAIL;
        }
    }

private:
    ~CEdgThumbProvider()
    {
        if (m_stream) m_stream->Release();
        InterlockedDecrement(&g_refs);
    }

    HRESULT ReadWholeStream(BYTE** outData, size_t* outSize)
    {
        STATSTG st = {};
        HRESULT hr = m_stream->Stat(&st, STATFLAG_NONAME);
        if (FAILED(hr)) return hr;
        if (st.cbSize.QuadPart == 0 ||
            st.cbSize.QuadPart > (ULONGLONG)kEdgMaxBytes)
            return E_FAIL;

        ULONG size = (ULONG)st.cbSize.QuadPart;
        BYTE* buf = new (std::nothrow) BYTE[size];
        if (!buf) return E_OUTOFMEMORY;

        LARGE_INTEGER zero = {};
        m_stream->Seek(zero, STREAM_SEEK_SET, nullptr);

        ULONG total = 0;
        while (total < size) {
            ULONG got = 0;
            hr = m_stream->Read(buf + total, size - total, &got);
            if (FAILED(hr) || got == 0) break;
            total += got;
        }
        if (total != size) { delete[] buf; return E_FAIL; }

        *outData = buf;
        *outSize = size;
        return S_OK;
    }

    LONG     m_refs;
    IStream* m_stream;
};

// ---------------------------------------------------------------- factory ---

class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : m_refs(1) { InterlockedIncrement(&g_refs); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_refs); }
    IFACEMETHODIMP_(ULONG) Release()
    {
        LONG n = InterlockedDecrement(&m_refs);
        if (n == 0) { InterlockedDecrement(&g_refs); delete this; }
        return n;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            CEdgThumbProvider* p = new (std::nothrow) CEdgThumbProvider();
            if (!p) return E_OUTOFMEMORY;
            HRESULT hr = p->QueryInterface(riid, ppv);
            p->Release();
            return hr;
        } catch (...) {
            return E_FAIL;
        }
    }
    IFACEMETHODIMP LockServer(BOOL lock)
    {
        if (lock) InterlockedIncrement(&g_refs); else InterlockedDecrement(&g_refs);
        return S_OK;
    }

private:
    LONG m_refs;
};

// ---------------------------------------------------------------- exports ---

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (rclsid != CLSID_EdgThumbProvider) return CLASS_E_CLASSNOTAVAILABLE;
    try {
        CClassFactory* f = new (std::nothrow) CClassFactory();
        if (!f) return E_OUTOFMEMORY;
        HRESULT hr = f->QueryInterface(riid, ppv);
        f->Release();
        return hr;
    } catch (...) {
        return E_FAIL;
    }
}

STDAPI DllCanUnloadNow()
{
    return (g_refs == 0) ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hInst);
    return TRUE;
}
