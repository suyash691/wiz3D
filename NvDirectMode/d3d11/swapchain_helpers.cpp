#include "swapchain_helpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>

// Output-mode flag from dllmain (avoids dragging log.h's d3d11 deps in here).
extern "C" int NvDM_OutputIsTopBottom();

// NOTE: this TU intentionally includes ONLY <windows.h> + <dxgi.h>.
// Do not pull in d3d11.h or any of the NvDirectMode proxy headers here —
// that's what tripped the MSVC parser cascade described in swapchain_helpers.h.

namespace NvDirectMode
{

const void* MakeDoubledSwapChainDesc(const void* pSwapChainDesc,
                                     unsigned int* outLogicalW,
                                     unsigned int* outLogicalH)
{
    if (outLogicalW) *outLogicalW = 0;
    if (outLogicalH) *outLogicalH = 0;
    if (!pSwapChainDesc) return nullptr;

    // Thread-local so concurrent calls don't clobber each other. Static
    // because the real CreateDeviceAndSwapChain call happens immediately
    // after this returns, so we just need stable storage for the duration
    // of that synchronous call.
    static thread_local DXGI_SWAP_CHAIN_DESC tlsDesc;
    tlsDesc = *(const DXGI_SWAP_CHAIN_DESC*)pSwapChainDesc;

    if (tlsDesc.BufferDesc.Width == 0 || tlsDesc.BufferDesc.Height == 0)
    {
        HWND hwnd = tlsDesc.OutputWindow;
        RECT rc = { 0 };
        if (hwnd && GetClientRect(hwnd, &rc))
        {
            if (tlsDesc.BufferDesc.Width  == 0) tlsDesc.BufferDesc.Width  = (UINT)(rc.right - rc.left);
            if (tlsDesc.BufferDesc.Height == 0) tlsDesc.BufferDesc.Height = (UINT)(rc.bottom - rc.top);
        }
    }

    if (outLogicalW) *outLogicalW = (unsigned int)tlsDesc.BufferDesc.Width;
    if (outLogicalH) *outLogicalH = (unsigned int)tlsDesc.BufferDesc.Height;

    // OutputMode controls which axis we double:
    //   T-B → backbuffer is W x 2H (eyes stacked)
    //   SBS → backbuffer is 2W x H (eyes side-by-side, default)
    if (NvDM_OutputIsTopBottom())
    {
        if (tlsDesc.BufferDesc.Height > 0) tlsDesc.BufferDesc.Height *= 2;
    }
    else
    {
        if (tlsDesc.BufferDesc.Width > 0)  tlsDesc.BufferDesc.Width  *= 2;
    }

    return &tlsDesc;
}

const void* MakeDoubledSwapChainDesc1(const void* pSwapChainDesc1,
                                      unsigned int* outLogicalW,
                                      unsigned int* outLogicalH)
{
    if (outLogicalW) *outLogicalW = 0;
    if (outLogicalH) *outLogicalH = 0;
    if (!pSwapChainDesc1) return nullptr;

    static thread_local DXGI_SWAP_CHAIN_DESC1 tlsDesc1;
    tlsDesc1 = *(const DXGI_SWAP_CHAIN_DESC1*)pSwapChainDesc1;

    // DESC1 doesn't carry an HWND so we can't resolve 0x0 against a
    // window's client rect. Production games always specify explicit
    // Width/Height for ForHwnd/ForCoreWindow/ForComposition paths.
    if (outLogicalW) *outLogicalW = (unsigned int)tlsDesc1.Width;
    if (outLogicalH) *outLogicalH = (unsigned int)tlsDesc1.Height;

    if (NvDM_OutputIsTopBottom())
    {
        if (tlsDesc1.Height > 0) tlsDesc1.Height *= 2;
    }
    else
    {
        if (tlsDesc1.Width > 0)  tlsDesc1.Width  *= 2;
    }
    return &tlsDesc1;
}

} // namespace NvDirectMode
