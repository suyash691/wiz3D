/* wiz3D d3d11 - COM-proxy factory bridge between the d3d11.dll proxy and the
 * Device11Proxy / Context11Proxy classes. Inputs/outputs typed as void* so the
 * d3d11.dll proxy TU stays free of d3d11.h (system header re-declares
 * D3D11CreateDevice without __declspec(dllexport), conflicting with our
 * exports). Mirrors NvDirectMode/d3d11/proxy_factory.h's role for the iZ3D
 * stereo wrapper Option B port.
 */

#pragma once

#include <guiddef.h>

// Private IID claimed by Device11Proxy::QueryInterface so other DLLs can
// detect "is this a wiz3D-wrapped device?" via the standard COM equality path
// without sharing any C++ symbols. The wiz3D dxgi.dll proxy declares its own
// const GUID with the same value and matches on memcmp.
EXTERN_C const GUID IID_wiz3D_Device11Proxy;

// Stage 3b: same pattern for the resource/view proxies. Methods that take an
// `ID3D11Resource*` / `ID3D11RenderTargetView*` / `ID3D11DepthStencilView*`
// at the COM boundary check for these IIDs to detect a wiz3D proxy at their
// input. If positive, the wrap call extracts the real underlying pointer (and
// the right-eye sibling if any) before forwarding to the real D3D11 runtime —
// the runtime would crash if handed our proxy directly since it doesn't
// understand the proxy's vtable layout beyond ID3D11* method signatures.
EXTERN_C const GUID IID_wiz3D_Texture2D11Proxy;
EXTERN_C const GUID IID_wiz3D_RTV11Proxy;
EXTERN_C const GUID IID_wiz3D_DSV11Proxy;
EXTERN_C const GUID IID_wiz3D_SwapChain11Proxy;
// Stage 3c.1: Buffer11Proxy private IID. Buffers aren't stereo-doubled
// (no right-eye sibling) but identity wrapping still matters for the 4c
// eye-shift heuristic, which uses a tag bit on the wrapped buffer to
// decide whether to apply per-eye CB modification.
EXTERN_C const GUID IID_wiz3D_Buffer11Proxy;

namespace wiz3d
{
    // Wraps the device + (optional) immediate context the system
    // D3D11CreateDevice path produced. Both inputs and outputs are passed
    // as void* so the d3d11.dll dllmain TU stays away from d3d11.h.
    //
    // *ppDeviceInOut    : in/out — system device on entry, Device11Proxy on exit
    // *ppContextInOut   : in/out — system immediate context on entry,
    //                     Context11Proxy on exit. May be NULL if the game
    //                     didn't ask for the context.
    void WrapD3D11DeviceAndContext(void** ppDeviceInOut, void** ppContextInOut);

    // Stage 3b: identify whether a COM pointer the game just handed us is one
    // of our wiz3D proxies. Returns the proxy interface pointer (still
    // AddRef'd by the caller's reference) on hit, nullptr if not ours. The
    // caller does NOT receive a new ref — just identity. Implemented inline
    // so any TU can use them without linking against the proxy classes.
    //
    // Usage pattern at a method entry:
    //   ID3D11Resource* realIn = pResource;
    //   if (auto* tex = TryUnwrapTexture2D(pResource)) realIn = UnwrapRealLeft(tex);
    //   m_real->SomeMethod(realIn, ...);
    //
    // The helpers QueryInterface the input for our private IID; on hit they
    // Release the QI'd ref (we don't need it, only the identity match) and
    // reinterpret_cast back to the proxy. Same pattern NvDirectMode uses for
    // IID_NvDM_Device11Proxy.

    // Forward-declarations so callers don't have to include the proxy headers
    // just to call the unwrap helpers. The .cpp side does include them.
    class Texture2D11Proxy;
    class RTV11Proxy;
    class DSV11Proxy;
    class Buffer11Proxy;

    Texture2D11Proxy* TryUnwrapTexture2D(struct ID3D11Resource* p);
    RTV11Proxy*       TryUnwrapRTV(struct ID3D11RenderTargetView* p);
    DSV11Proxy*       TryUnwrapDSV(struct ID3D11DepthStencilView* p);
    Buffer11Proxy*    TryUnwrapBuffer(struct ID3D11Resource* p);
}

// Exported entry point the d3d11.dll proxy resolves via GetProcAddress, so
// the existing wiz3D-proxy/d3d11/dllmain doesn't need to be linked against
// S3DWrapperD3D10.lib directly. Uses __cdecl (the default) rather than WINAPI
// so the exported symbol name matches across x86/x64 — __stdcall would
// decorate to _name@N on x86 and break the bare-name def-file entry.
extern "C" __declspec(dllexport) void
wiz3D_WrapD3D11DeviceAndContext(void** ppDeviceInOut, void** ppContextInOut);

// Stage 4b.3: companion export for D3D11CreateDeviceAndSwapChain's swap
// chain output. Called AFTER wiz3D_WrapD3D11DeviceAndContext has run, so
// *ppDeviceInOut is already our Device11Proxy. Wraps the real swap chain
// in SwapChain11Proxy and substitutes it in-place. No-op when either
// pointer is null. Pure passthrough for the eventual frame-boundary
// callback (4b.4 onward): present-time frame-end hook + 4d's SBS composite.
extern "C" __declspec(dllexport) void
wiz3D_WrapSwapChain(void** ppSwapChainInOut, void* pWrappedDevice);
