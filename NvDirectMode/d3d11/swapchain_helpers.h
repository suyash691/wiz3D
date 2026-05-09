/* NvDirectMode d3d11 - DXGI_SWAP_CHAIN_DESC mutation helper
 *
 * Isolated TU that touches dxgi.h and produces a doubled-width copy of the
 * game's swap-chain desc for stage 1b-iii. Kept deliberately separate from
 * proxy_factory.cpp / Device11Proxy.cpp / SwapChainProxy.cpp so the d3d11.h
 * include chain doesn't tangle with the proxy class headers (a previous
 * attempt at putting this logic into proxy_factory.cpp triggered an MSVC
 * parser cascade in SwapChainProxy.h that we couldn't pin down).
 *
 * Public interface uses void* throughout so dllmain.cpp can call this
 * without pulling in d3d11.h / dxgi.h itself (which would clash with our
 * own __declspec(dllexport) re-declarations of D3D11CreateDevice etc.).
 */

#pragma once

namespace NvDirectMode
{
    // Returns a pointer to a thread-local mutated copy of the input desc:
    //   - any zero BackBufferWidth/Height resolved against OutputWindow's client area
    //   - BackBufferWidth doubled (so the real swap chain holds a side-by-side
    //     surface for both eyes)
    //
    // *outLogicalW / *outLogicalH receive the original (one-eye) dimensions.
    //
    // Lifetime: the returned pointer is valid until the next call to this
    // function on the same thread. Since the real D3D11CreateDeviceAndSwapChain
    // call is made synchronously immediately after, this is sufficient.
    //
    // pSwapChainDesc may be NULL — in that case returns NULL and zeros the
    // out-params.
    const void* MakeDoubledSwapChainDesc(const void* pSwapChainDesc,
                                         unsigned int* outLogicalW,
                                         unsigned int* outLogicalH);

    // Variant for DXGI_SWAP_CHAIN_DESC1 (no BufferDesc nesting; Width/Height
    // live at the top level). Used by IDXGIFactory2's CreateSwapChainForHwnd
    // / CreateSwapChainForCoreWindow / CreateSwapChainForComposition. Falls
    // back to a 1x1 sentinel when both Width and Height are zero on input
    // since we can't resolve from a window in that case (Hwnd vs CoreWindow
    // vs composition surface paths all differ); production games always
    // specify explicit dimensions.
    const void* MakeDoubledSwapChainDesc1(const void* pSwapChainDesc1,
                                          unsigned int* outLogicalW,
                                          unsigned int* outLogicalH);
}
