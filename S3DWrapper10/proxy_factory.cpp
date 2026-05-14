/* wiz3D d3d11 - COM-proxy factory bridge implementation (Option B Stage 2).
 *
 * Ported pattern from NvDirectMode/d3d11/proxy_factory.cpp. The export
 * wiz3D_WrapD3D11DeviceAndContext is the integration point the d3d11.dll
 * proxy (wiz3D-proxy/d3d11/dllmain.cpp) calls right after the real
 * D3D11CreateDevice returns, swapping the returned device + immediate
 * context for our wrappers before the game sees them.
 */

#include "StdAfx.h"
#include "proxy_factory.h"
#include "Device11Proxy.h"
#include "Context11Proxy.h"
#include "Texture2D11Proxy.h"
#include "RTV11Proxy.h"
#include "DSV11Proxy.h"
#include "Buffer11Proxy.h"
#include "SwapChain11Proxy.h"
#include "AdapterFunctions.h"  // DDILog

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// {3D8F1B2E-7A4C-4D6E-B1F0-8C3A9D2E5F6B}
// Private GUID claimed by Device11Proxy::QueryInterface so cross-DLL identity
// checks (the dxgi.dll proxy probing whether a CreateSwapChain device is one
// of ours) work via memcmp on the IID alone — no shared symbols needed.
EXTERN_C const GUID IID_wiz3D_Device11Proxy =
    { 0x3D8F1B2E, 0x7A4C, 0x4D6E, { 0xB1, 0xF0, 0x8C, 0x3A, 0x9D, 0x2E, 0x5F, 0x6B } };

// Stage 3b: private GUIDs for the resource/view proxies. Used by the
// Try*Unwrap helpers to identify wiz3D wrappers passed at COM boundaries
// before forwarding to the real D3D11 runtime. Same memcmp-on-IID pattern
// keeps every TU that needs to unwrap free of header dependencies on the
// concrete proxy classes.
// {C8E94A12-3B5D-4F2A-9D1C-7A6B8E3F4C5D}
EXTERN_C const GUID IID_wiz3D_Texture2D11Proxy =
    { 0xC8E94A12, 0x3B5D, 0x4F2A, { 0x9D, 0x1C, 0x7A, 0x6B, 0x8E, 0x3F, 0x4C, 0x5D } };
// {D9F05B23-4C6E-5A3B-AE2D-8B7C9F4D5E6E}
EXTERN_C const GUID IID_wiz3D_RTV11Proxy =
    { 0xD9F05B23, 0x4C6E, 0x5A3B, { 0xAE, 0x2D, 0x8B, 0x7C, 0x9F, 0x4D, 0x5E, 0x6E } };
// {EAF16C34-5D7F-6B4C-BF3E-9C8DAF5E6F7F}
EXTERN_C const GUID IID_wiz3D_DSV11Proxy =
    { 0xEAF16C34, 0x5D7F, 0x6B4C, { 0xBF, 0x3E, 0x9C, 0x8D, 0xAF, 0x5E, 0x6F, 0x7F } };
// {FB027D45-6E80-7C5D-CF4F-AD9EBF6F708F}
EXTERN_C const GUID IID_wiz3D_SwapChain11Proxy =
    { 0xFB027D45, 0x6E80, 0x7C5D, { 0xCF, 0x4F, 0xAD, 0x9E, 0xBF, 0x6F, 0x70, 0x8F } };
// {0C138E56-7F91-8D6E-DF50-BEAFC07F819F}
EXTERN_C const GUID IID_wiz3D_Buffer11Proxy =
    { 0x0C138E56, 0x7F91, 0x8D6E, { 0xDF, 0x50, 0xBE, 0xAF, 0xC0, 0x7F, 0x81, 0x9F } };

namespace wiz3d
{

void WrapD3D11DeviceAndContext(void** ppDeviceInOut, void** ppContextInOut)
{
    if (!ppDeviceInOut || !*ppDeviceInOut) return;

    auto* realDevice  = static_cast<ID3D11Device*>(*ppDeviceInOut);
    auto* deviceProxy = new Device11Proxy(realDevice);
    DDILog("WrapD3D11DeviceAndContext: realDevice=%p -> wiz3d::Device11Proxy=%p\n",
           realDevice, deviceProxy);

    if (ppContextInOut && *ppContextInOut)
    {
        auto* realCtx  = static_cast<ID3D11DeviceContext*>(*ppContextInOut);
        auto* ctxProxy = new Context11Proxy(realCtx, deviceProxy);
        deviceProxy->SetImmediateContextProxy(ctxProxy);
        DDILog("WrapD3D11DeviceAndContext: realCtx=%p -> wiz3d::Context11Proxy=%p\n",
               realCtx, ctxProxy);
        *ppContextInOut = static_cast<ID3D11DeviceContext*>(ctxProxy);
    }

    *ppDeviceInOut = static_cast<ID3D11Device*>(deviceProxy);
}

// COM single-inheritance: the IUnknown* returned via the private-IID QI has
// the same address as the proxy's `this`. Casting back to the concrete proxy
// type is a base→derived narrowing that's well-defined when the pointer's
// dynamic type matches — which it does here because only the proxy claims
// that private IID. C-style cast bypasses the C4946 "reinterpret between
// related classes" warning (the proxies inherit from IUnknown via ID3D11Xxx,
// so the compiler can see the relationship and reinterpret_cast triggers
// W4-as-error in this project). Standard idiom for COM private-IID lookups.

Texture2D11Proxy* TryUnwrapTexture2D(ID3D11Resource* p)
{
    if (!p) return nullptr;
    IUnknown* probe = nullptr;
    if (FAILED(p->QueryInterface(IID_wiz3D_Texture2D11Proxy,
                                  reinterpret_cast<void**>(&probe))) || !probe)
        return nullptr;
    probe->Release();   // drop QI ref — we only care about identity
    return (Texture2D11Proxy*)probe;
}

RTV11Proxy* TryUnwrapRTV(ID3D11RenderTargetView* p)
{
    if (!p) return nullptr;
    IUnknown* probe = nullptr;
    if (FAILED(p->QueryInterface(IID_wiz3D_RTV11Proxy,
                                  reinterpret_cast<void**>(&probe))) || !probe)
        return nullptr;
    probe->Release();
    return (RTV11Proxy*)probe;
}

DSV11Proxy* TryUnwrapDSV(ID3D11DepthStencilView* p)
{
    if (!p) return nullptr;
    IUnknown* probe = nullptr;
    if (FAILED(p->QueryInterface(IID_wiz3D_DSV11Proxy,
                                  reinterpret_cast<void**>(&probe))) || !probe)
        return nullptr;
    probe->Release();
    return (DSV11Proxy*)probe;
}

Buffer11Proxy* TryUnwrapBuffer(ID3D11Resource* p)
{
    if (!p) return nullptr;
    IUnknown* probe = nullptr;
    if (FAILED(p->QueryInterface(IID_wiz3D_Buffer11Proxy,
                                  reinterpret_cast<void**>(&probe))) || !probe)
        return nullptr;
    probe->Release();
    return (Buffer11Proxy*)probe;
}

} // namespace wiz3d

extern "C" __declspec(dllexport) void
wiz3D_WrapD3D11DeviceAndContext(void** ppDeviceInOut, void** ppContextInOut)
{
    wiz3d::WrapD3D11DeviceAndContext(ppDeviceInOut, ppContextInOut);
}

extern "C" __declspec(dllexport) void
wiz3D_WrapSwapChain(void** ppSwapChainInOut, void* pWrappedDevice)
{
    if (!ppSwapChainInOut || !*ppSwapChainInOut) return;
    if (!pWrappedDevice) return;

    // Stage 4b.3 safety gate: BioShock (UE2.5-era engine) crashes inside the
    // first method call on our SwapChain11Proxy because its rendering code
    // walks swap-chain struct internals past the COM vtable — same TR2013 /
    // Lost Planet / Hitman class of bug iZ3D ran into pre-NvDirectMode. Our
    // proxy's member layout doesn't match the real DXGI swap chain, so the
    // game reads garbage and crashes on ECX=1. Until Stage 4d revisits with
    // either vtable hot-patching or a game-by-game enable, default this off
    // so games that don't need the Present hook (i.e. all of them in
    // Stages 1-3 + 4b) keep working unmodified.
    if (!gInfo.UseCOMWrapSwapChain)
    {
        DDILog("wiz3D_WrapSwapChain: UseCOMWrapSwapChain=0 -- passing through unwrapped (game keeps real SC)\n");
        return;
    }

    // Cast pWrappedDevice back to Device11Proxy. The caller (d3d11.dll
    // proxy's D3D11CreateDeviceAndSwapChain) passes the wrapped device
    // returned from wiz3D_WrapD3D11DeviceAndContext, so this is safe — the
    // pointer IS our proxy.
    auto* deviceProxy = reinterpret_cast<wiz3d::Device11Proxy*>(pWrappedDevice);
    auto* realSC      = static_cast<IDXGISwapChain*>(*ppSwapChainInOut);

    // Try to also get an IDXGISwapChain1 (Win8+ platform). Failure is fine,
    // SwapChain11Proxy is tolerant — it returns E_NOINTERFACE for the 1+
    // methods when the QI didn't succeed.
    IDXGISwapChain1* realSC1 = nullptr;
    realSC->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&realSC1));

    auto* scProxy = new wiz3d::SwapChain11Proxy(realSC, realSC1, deviceProxy);
    DDILog("wiz3D_WrapSwapChain: realSC=%p realSC1=%p -> SwapChain11Proxy=%p (device=%p)\n",
           realSC, realSC1, scProxy, deviceProxy);
    *ppSwapChainInOut = static_cast<IDXGISwapChain*>(scProxy);
}
