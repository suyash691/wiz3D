#include "proxy_factory.h"
#include "Device11Proxy.h"
#include "Context11Proxy.h"
#include "SwapChainProxy.h"
#include "swapchain_helpers.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// {A7B3C5D9-1E2F-4F8B-9C3D-4FA1B2C3D4E5}  — NvDirectMode private GUID
// claimed by Device11Proxy::QueryInterface. Cross-DLL identity check
// from dxgi.dll: same value declared there as a separate const, matched
// by memcmp via standard COM IID equality.
EXTERN_C const GUID IID_NvDM_Device11Proxy =
    { 0xA7B3C5D9, 0x1E2F, 0x4F8B, { 0x9C, 0x3D, 0x4F, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5 } };

namespace NvDirectMode
{

void WrapD3D11DeviceAndContext(void** ppDeviceInOut, void** ppContextInOut)
{
    if (!ppDeviceInOut || !*ppDeviceInOut) return;

    auto* realDevice  = static_cast<ID3D11Device*>(*ppDeviceInOut);
    auto* deviceProxy = new Device11Proxy(realDevice);
    LOG_VERBOSE("  WrapD3D11DeviceAndContext: realDevice=%p -> Device11Proxy=%p\n",
                realDevice, deviceProxy);

    if (ppContextInOut && *ppContextInOut)
    {
        auto* realCtx  = static_cast<ID3D11DeviceContext*>(*ppContextInOut);
        auto* ctxProxy = new Context11Proxy(realCtx, deviceProxy);
        deviceProxy->SetImmediateContextProxy(ctxProxy);
        LOG_VERBOSE("  WrapD3D11DeviceAndContext: realCtx=%p -> Context11Proxy=%p\n",
                    realCtx, ctxProxy);
        *ppContextInOut = static_cast<ID3D11DeviceContext*>(ctxProxy);
    }

    *ppDeviceInOut = static_cast<ID3D11Device*>(deviceProxy);
}

void* WrapDXGISwapChain(void* realSwapChain, void* wrappedDevice)
{
    if (!realSwapChain) return nullptr;
    auto* sc       = static_cast<IDXGISwapChain*>(realSwapChain);
    auto* devProxy = static_cast<Device11Proxy*>(wrappedDevice);
    auto* scProxy  = new SwapChainProxy(sc, devProxy);
    LOG_VERBOSE("  WrapDXGISwapChain: realSwapChain=%p -> SwapChainProxy=%p (parent dev=%p)\n",
                sc, scProxy, devProxy);
    return static_cast<IDXGISwapChain*>(scProxy);
}

void SetWrappedDeviceLogicalSize(void* wrappedDevice, unsigned int w, unsigned int h)
{
    if (!wrappedDevice) return;
    static_cast<Device11Proxy*>(wrappedDevice)->SetLogicalBackBufferSize(w, h);
    LOG_VERBOSE("  SetWrappedDeviceLogicalSize: dev=%p logical=%ux%u\n", wrappedDevice, w, h);
}

} // namespace NvDirectMode

// ---------------------------------------------------------------------------
// Cross-DLL bridge — called by dxgi.dll's factory-wrap path. dxgi.dll resolves
// this via GetProcAddress at runtime; passing void* through the boundary keeps
// d3d11.h / dxgi.h out of dxgi.dll's TUs.
//
// Contract:
//   deviceOrCommandQueue : the IUnknown the game passed to
//                          IDXGIFactory::CreateSwapChain*. We probe with the
//                          private IID_NvDM_Device11Proxy to see if it's our
//                          Device11Proxy.
//   realSC               : the real IDXGISwapChain returned by the real
//                          factory (after the doubled desc).
//   logicalW / logicalH  : the *original* (one-eye) dimensions from the
//                          game's desc, before doubling.
//
// Returns:
//   - wrapped IDXGISwapChain* (cast to void*) on success — caller substitutes
//     this for the real swap chain when handing it back to the game
//   - nullptr if `deviceOrCommandQueue` isn't one of our wrapped devices
//     (caller falls back to returning the unwrapped real swap chain)
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) void* WINAPI
NvDM_WrapAndRegisterSwapChain(void* deviceOrCommandQueue,
                              void* realSC,
                              unsigned int logicalW,
                              unsigned int logicalH)
{
    if (!deviceOrCommandQueue || !realSC) return nullptr;

    auto* dev = static_cast<IUnknown*>(deviceOrCommandQueue);
    auto* sc  = static_cast<IDXGISwapChain*>(realSC);

    // Identify by private IID — only Device11Proxy claims it.
    IUnknown* probe = nullptr;
    HRESULT hr = dev->QueryInterface(IID_NvDM_Device11Proxy,
                                      reinterpret_cast<void**>(&probe));
    if (FAILED(hr) || !probe)
    {
        NvDM_Log("  NvDM_WrapAndRegisterSwapChain: dev=%p NOT a wrapped device (hr=0x%08lX) -- returning unwrapped\n",
                               deviceOrCommandQueue, hr);
        return nullptr;
    }

    // Single-inheritance COM: the IUnknown returned IS the Device11Proxy
    // base class. reinterpret_cast back is safe (and the standard pattern
    // for private-IID identity checks).
    auto* dp = reinterpret_cast<NvDirectMode::Device11Proxy*>(probe);
    NvDM_Log("  NvDM_WrapAndRegisterSwapChain: dev=%p IS Device11Proxy=%p; wrapping realSC=%p logical=%ux%u\n",
                           deviceOrCommandQueue, dp, sc, logicalW, logicalH);

    auto* scProxy = new NvDirectMode::SwapChainProxy(sc, dp);
    if (logicalW > 0 && logicalH > 0)
        dp->SetLogicalBackBufferSize(logicalW, logicalH);

    probe->Release();   // drop the QI ref we took for the identity check
    return static_cast<IDXGISwapChain*>(scProxy);
}

// Doubled-desc helper exposed cross-DLL — same logic as
// swapchain_helpers' MakeDoubledSwapChainDesc, but exported so dxgi.dll
// shares one canonical implementation.
extern "C" __declspec(dllexport) const void* WINAPI
NvDM_DoubleSwapChainDesc(const void* pDesc,
                         unsigned int* outLogicalW,
                         unsigned int* outLogicalH)
{
    return NvDirectMode::MakeDoubledSwapChainDesc(pDesc, outLogicalW, outLogicalH);
}

extern "C" __declspec(dllexport) const void* WINAPI
NvDM_DoubleSwapChainDesc1(const void* pDesc1,
                          unsigned int* outLogicalW,
                          unsigned int* outLogicalH)
{
    return NvDirectMode::MakeDoubledSwapChainDesc1(pDesc1, outLogicalW, outLogicalH);
}
