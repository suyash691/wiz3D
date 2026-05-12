/* NvDirectMode/d3d9 - NVIDIA 3D Vision magic-header SBS capture impl. */

#include "magic_header_capture.h"
#include "log.h"

namespace NvDirectMode {
namespace MagicHeader {

namespace {

// Read the last row of a lockable surface to check for the NV stereo signature.
// On success out_flags is populated and returns true.
bool ReadMagicViaLock(IDirect3DSurface9* pSurface, UINT srcWidth, UINT srcHeight,
                     unsigned int& out_flags)
{
    // Lock just the last row (1 pixel tall) for read-only access. Most
    // magic-tagged surfaces are created Lockable=TRUE precisely so the
    // producer can write the header — so this lock should succeed cheaply.
    RECT row = { 0, (LONG)(srcHeight - 1), (LONG)srcWidth, (LONG)srcHeight };
    D3DLOCKED_RECT lr = {};
    HRESULT hr = pSurface->LockRect(&lr, &row, D3DLOCK_READONLY);
    if (FAILED(hr) || !lr.pBits)
        return false;

    const NvStereoImageHeader* hdr =
        reinterpret_cast<const NvStereoImageHeader*>(lr.pBits);
    bool magic = (hdr->dwSignature == kSignature);
    if (magic)
        out_flags = hdr->dwFlags;

    pSurface->UnlockRect();
    return magic;
}

// Fallback: copy the source to a SCRATCH-pool system-mem surface and read.
// Slower (one full surface copy + lock), but works on non-lockable RTs.
// Only used when the direct lock above fails.
bool ReadMagicViaCopy(IDirect3DDevice9* pDevice, IDirect3DSurface9* pSurface,
                     const D3DSURFACE_DESC& desc, unsigned int& out_flags)
{
    IDirect3DSurface9* sysSurf = nullptr;
    HRESULT hr = pDevice->CreateOffscreenPlainSurface(
        desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
        &sysSurf, NULL);
    if (FAILED(hr) || !sysSurf)
        return false;

    // GetRenderTargetData requires the source be a render target. If the
    // game's stereo surface isn't an RT (unusual — Helifax's IS), we have
    // no cheap way to read it; treat as no-magic.
    hr = pDevice->GetRenderTargetData(pSurface, sysSurf);
    if (FAILED(hr))
    {
        sysSurf->Release();
        return false;
    }

    bool magic = ReadMagicViaLock(sysSurf, desc.Width, desc.Height, out_flags);
    sysSurf->Release();
    return magic;
}

} // anonymous namespace

DetectResult DetectStereoMagic(IDirect3DDevice9* pDevice,
                               IDirect3DSurface9* pSurface)
{
    DetectResult r = { false, false, 0, 0 };
    if (!pDevice || !pSurface) return r;

    D3DSURFACE_DESC desc = {};
    if (FAILED(pSurface->GetDesc(&desc))) return r;

    // Magic-header surfaces are 2W × H+1. Width must be even and at least 4
    // (NV writes a 20-byte / 5-DWORD header so the surface needs to be wide
    // enough — but in practice all real surfaces are huge so this is just a
    // sanity guard against random small surfaces). Height >= 2 so there's
    // at least one row of image content plus the header row.
    if (desc.Width < 4 || (desc.Width & 1) || desc.Height < 2)
        return r;

    unsigned int flags = 0;
    bool found = ReadMagicViaLock(pSurface, desc.Width, desc.Height, flags);
    if (!found)
        found = ReadMagicViaCopy(pDevice, pSurface, desc, flags);
    if (!found)
        return r;

    r.hasMagic  = true;
    r.swapEyes  = (flags & kFlagSwapEyes) != 0;
    r.eyeWidth  = desc.Width / 2;
    r.eyeHeight = desc.Height - 1;
    LOG_VERBOSE("  d3d9 MagicHeader: detected NV3D on surf=%p, %ux%u eye, flags=0x%08X (swapEyes=%d)\n",
                (void*)pSurface, r.eyeWidth, r.eyeHeight, flags, r.swapEyes);
    return r;
}

HRESULT SplitStereoSurface(IDirect3DDevice9* pDevice,
                           IDirect3DSurface9* pSrc,
                           UINT eyeWidth, UINT eyeHeight,
                           IDirect3DSurface9* pLeftDst,
                           IDirect3DSurface9* pRightDst,
                           bool swapEyes)
{
    if (!pDevice || !pSrc || !pLeftDst || !pRightDst) return E_POINTER;

    // RECT is {left, top, right, bottom}.
    // Left eye occupies columns [0..W), right eye [W..2W), both rows [0..H).
    RECT srcLeft  = { 0,              0, (LONG)eyeWidth,       (LONG)eyeHeight };
    RECT srcRight = { (LONG)eyeWidth, 0, (LONG)(eyeWidth * 2), (LONG)eyeHeight };

    IDirect3DSurface9* leftSrcOnto  = swapEyes ? pRightDst : pLeftDst;
    IDirect3DSurface9* rightSrcOnto = swapEyes ? pLeftDst  : pRightDst;

    HRESULT hr = pDevice->StretchRect(pSrc, &srcLeft,  leftSrcOnto,  NULL, D3DTEXF_LINEAR);
    if (FAILED(hr)) return hr;
    return pDevice->StretchRect(pSrc, &srcRight, rightSrcOnto, NULL, D3DTEXF_LINEAR);
}

} // namespace MagicHeader
} // namespace NvDirectMode
