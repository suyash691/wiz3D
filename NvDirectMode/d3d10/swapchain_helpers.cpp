#include "swapchain_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>

extern "C" int NvDM_OutputIsTopBottom();

namespace NvDirectMode
{

const void* MakeDoubledSwapChainDesc(const void* pSwapChainDesc,
                                     unsigned int* outLogicalW,
                                     unsigned int* outLogicalH)
{
    // Stage 3 shadow-RT (mirrors d3d11): no longer doubles the desc. Real
    // BB stays at the game's requested (one-eye) size; the doubled
    // per-eye rendering surface is allocated as a side ID3D10Texture2D
    // managed by SwapChainProxy::EnsureShadowBB. This helper now just
    // extracts the size from the desc; the desc itself is returned
    // unchanged.
    if (outLogicalW) *outLogicalW = 0;
    if (outLogicalH) *outLogicalH = 0;
    if (!pSwapChainDesc) return nullptr;

    auto* d = (const DXGI_SWAP_CHAIN_DESC*)pSwapChainDesc;
    UINT w = d->BufferDesc.Width;
    UINT h = d->BufferDesc.Height;
    if ((w == 0 || h == 0) && d->OutputWindow)
    {
        RECT rc = { 0 };
        if (GetClientRect(d->OutputWindow, &rc))
        {
            if (w == 0) w = (UINT)(rc.right - rc.left);
            if (h == 0) h = (UINT)(rc.bottom - rc.top);
        }
    }
    if (outLogicalW) *outLogicalW = (unsigned int)w;
    if (outLogicalH) *outLogicalH = (unsigned int)h;
    return pSwapChainDesc;
}

} // namespace NvDirectMode
