/* NvDirectMode d3d11 - thin factory bridge between dllmain.cpp and the COM
 * proxies. Same rationale as the d3d9 proxy_factory.h: dllmain.cpp must NOT
 * include d3d11.h (system header would re-declare D3D11CreateDevice without
 * dllexport, conflicting with our extern "C" __declspec(dllexport) version).
 */

#pragma once

#include <guiddef.h>

// Private IID claimed by NvDirectMode's Device11Proxy. The dxgi.dll proxy
// (separate DLL) QIs incoming devices for this IID to detect whether a
// CreateSwapChain*-time `pDevice` is one of our wrapped Device11Proxy
// instances. Match-on-equality (no shared symbol resolution) — the dxgi
// proxy declares its own const GUID with the same value.
EXTERN_C const GUID IID_NvDM_Device11Proxy;

namespace NvDirectMode
{
    // Wraps the device + (optional) immediate context that the system
    // D3D11CreateDevice path produced. Both inputs and outputs are passed
    // as void* so dllmain stays away from d3d11.h.
    //
    // *ppDeviceInOut    : in/out — system device on entry, wrapped on exit
    // *ppContextInOut   : in/out — system immediate context on entry,
    //                     wrapped on exit. May be NULL if game didn't ask
    //                     for the context.
    void WrapD3D11DeviceAndContext(void** ppDeviceInOut, void** ppContextInOut);

    // Wrap a swap chain produced via D3D11CreateDeviceAndSwapChain. The
    // device pointer must already be a NvDirectMode-wrapped Device11Proxy
    // (so we link the swap chain back to its parent).
    void* WrapDXGISwapChain(void* realSwapChain, void* wrappedDevice);

    // 1b-iii: tag a wrapped device with the game's perceived (one-eye)
    // backbuffer dimensions, set during DXGI swap-chain creation. Used by
    // 1b-iv's OMSetRenderTargets viewport routing.
    void SetWrappedDeviceLogicalSize(void* wrappedDevice, unsigned int w, unsigned int h);
}
