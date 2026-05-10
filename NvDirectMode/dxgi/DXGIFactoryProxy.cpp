#include "DXGIFactoryProxy.h"
#include "adapter_vtable_hooks.h"

#include <stdio.h>
#pragma comment(lib, "dxguid.lib")

// dllmain.cpp owns the log file. Forward-declared here.
extern "C" void NvDM_DxgiLog(const char* fmt, ...);

namespace NvDirectMode
{

// ---------------------------------------------------------------------------
// d3d11.dll cross-DLL bridge (resolved lazily via GetProcAddress)
// ---------------------------------------------------------------------------
typedef void* (WINAPI *pfnNvDM_WrapAndRegisterSwapChain)(
    void* deviceOrCommandQueue, void* realSC,
    unsigned int logicalW, unsigned int logicalH);
typedef const void* (WINAPI *pfnNvDM_DoubleSwapChainDesc)(
    const void* pDesc, unsigned int* outLogicalW, unsigned int* outLogicalH);
typedef const void* (WINAPI *pfnNvDM_DoubleSwapChainDesc1)(
    const void* pDesc1, unsigned int* outLogicalW, unsigned int* outLogicalH);

typedef void* (WINAPI *pfnNvDM_GetRealDevice)(void* maybeWrapped);

static pfnNvDM_WrapAndRegisterSwapChain g_pfnWrap = nullptr;
static pfnNvDM_DoubleSwapChainDesc      g_pfnDoubleDesc  = nullptr;
static pfnNvDM_DoubleSwapChainDesc1     g_pfnDoubleDesc1 = nullptr;
static pfnNvDM_GetRealDevice            g_pfnGetReal     = nullptr;

// Helper: try to unwrap the device passed to factory CreateSwapChain*
// before handing it to system DXGI (which crashes when it walks our
// wrapped Device11Proxy's struct internals). Returns the real device
// pointer with a fresh ref if unwrapped, or the input pointer if not
// (and outOwnsRef = false).
static IUnknown* UnwrapDeviceForRealCall(IUnknown* pDeviceIn, bool& outOwnsRef)
{
    outOwnsRef = false;
    if (!g_pfnGetReal || !pDeviceIn) return pDeviceIn;
    IUnknown* real = static_cast<IUnknown*>(g_pfnGetReal(pDeviceIn));
    if (real) { outOwnsRef = true; return real; }
    return pDeviceIn;
}
static volatile LONG                    g_bridgeProbed   = 0;

static void EnsureBridgeLoaded()
{
    if (InterlockedCompareExchange(&g_bridgeProbed, 1, 0) != 0) return;
    HMODULE h = GetModuleHandleW(L"d3d11.dll");
    if (!h) return;
    g_pfnWrap        = (pfnNvDM_WrapAndRegisterSwapChain) GetProcAddress(h, "NvDM_WrapAndRegisterSwapChain");
    g_pfnDoubleDesc  = (pfnNvDM_DoubleSwapChainDesc)      GetProcAddress(h, "NvDM_DoubleSwapChainDesc");
    g_pfnDoubleDesc1 = (pfnNvDM_DoubleSwapChainDesc1)     GetProcAddress(h, "NvDM_DoubleSwapChainDesc1");
    g_pfnGetReal     = (pfnNvDM_GetRealDevice)            GetProcAddress(h, "NvDM_GetRealDevice");
    NvDM_DxgiLog("  bridge: d3d11.dll handle=%p  Wrap=%p  Double=%p  Double1=%p  GetReal=%p\n",
                 h, (void*)g_pfnWrap, (void*)g_pfnDoubleDesc, (void*)g_pfnDoubleDesc1,
                 (void*)g_pfnGetReal);
}

// ---------------------------------------------------------------------------
// DXGIFactoryProxy
// ---------------------------------------------------------------------------
DXGIFactoryProxy::DXGIFactoryProxy(IDXGIFactory* r0, IDXGIFactory1* r1, IDXGIFactory2* r2)
    : m_real0(r0), m_real1(r1), m_real2(r2), m_refs(1)
{
    NvDM_DxgiLog("  DXGIFactoryProxy ctor: real0=%p real1=%p real2=%p\n", r0, r1, r2);
}

DXGIFactoryProxy::~DXGIFactoryProxy()
{
    if (m_real2) { m_real2->Release(); m_real2 = nullptr; }
    if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
    if (m_real0) { m_real0->Release(); m_real0 = nullptr; }
}

ULONG STDMETHODCALLTYPE DXGIFactoryProxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE DXGIFactoryProxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_IDXGIObject ||
        riid == IID_IDXGIFactory)
    {
        *ppvObj = static_cast<IDXGIFactory*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIFactory1 && m_real1)
    {
        *ppvObj = static_cast<IDXGIFactory1*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIFactory2 && m_real2)
    {
        *ppvObj = static_cast<IDXGIFactory2*>(this);
        AddRef();
        return S_OK;
    }
    // IDXGIFactory3+ — return E_NOINTERFACE so games fall back to a level
    // we wrap. Otherwise game would QI for the unwrapped IDXGIFactory3+
    // and call CreateSwapChain* on it, bypassing this wrapper entirely.
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// EnumAdapters / EnumAdapters1: now passthrough — see header comment.
// (Wrapping these crashed system d3d11.dll's internal device-creation
// code which walks adapter struct internals past the vtable.)

// ---------------------------------------------------------------------------
// Intercepted swap-chain creation methods
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE DXGIFactoryProxy::CreateSwapChain(
    IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
    NvDM_DxgiLog("  IDXGIFactory::CreateSwapChain(pDevice=%p, pDesc=%p)\n", (void*)pDevice, (void*)pDesc);
    if (!pDesc || !ppSwapChain) return m_real0->CreateSwapChain(pDevice, pDesc, ppSwapChain);

    EnsureBridgeLoaded();

    // Double the desc via d3d11.dll's helper if available; otherwise call
    // through with the original desc (we won't be able to wrap anyway).
    unsigned int logicalW = pDesc->BufferDesc.Width;
    unsigned int logicalH = pDesc->BufferDesc.Height;
    DXGI_SWAP_CHAIN_DESC localDesc = *pDesc;
    const DXGI_SWAP_CHAIN_DESC* useDesc = pDesc;
    if (g_pfnDoubleDesc)
    {
        const void* doubled = g_pfnDoubleDesc(&localDesc, &logicalW, &logicalH);
        if (doubled) useDesc = static_cast<const DXGI_SWAP_CHAIN_DESC*>(doubled);
    }

    // Pass REAL device to system DXGI — system walks the device's struct
    // internals and crashes on our Device11Proxy layout.
    bool ownsRealRef = false;
    IUnknown* realDevice = UnwrapDeviceForRealCall(pDevice, ownsRealRef);
    HRESULT hr = m_real0->CreateSwapChain(realDevice,
                                           const_cast<DXGI_SWAP_CHAIN_DESC*>(useDesc),
                                           ppSwapChain);
    if (ownsRealRef && realDevice) realDevice->Release();
    NvDM_DxgiLog("    real CreateSwapChain hr=0x%08lX  realSC=%p  logical=%ux%u  unwrapped=%d\n",
                 hr, ppSwapChain ? (void*)*ppSwapChain : nullptr, logicalW, logicalH, (int)ownsRealRef);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && g_pfnWrap)
    {
        void* wrapped = g_pfnWrap(pDevice, *ppSwapChain, logicalW, logicalH);
        if (wrapped)
        {
            *ppSwapChain = static_cast<IDXGISwapChain*>(wrapped);
            NvDM_DxgiLog("    wrapped via d3d11.dll bridge -> %p\n", wrapped);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIFactoryProxy::CreateSwapChainForHwnd(
    IUnknown* pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain)
{
    NvDM_DxgiLog("  IDXGIFactory2::CreateSwapChainForHwnd(pDevice=%p, hWnd=%p, pDesc=%p)\n",
                 (void*)pDevice, (void*)hWnd, (void*)pDesc);
    if (!m_real2) return E_NOINTERFACE;
    if (!pDesc || !ppSwapChain)
        return m_real2->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);

    EnsureBridgeLoaded();

    unsigned int logicalW = pDesc->Width;
    unsigned int logicalH = pDesc->Height;
    DXGI_SWAP_CHAIN_DESC1 localDesc = *pDesc;
    const DXGI_SWAP_CHAIN_DESC1* useDesc = pDesc;
    if (g_pfnDoubleDesc1)
    {
        const void* doubled = g_pfnDoubleDesc1(&localDesc, &logicalW, &logicalH);
        if (doubled) useDesc = static_cast<const DXGI_SWAP_CHAIN_DESC1*>(doubled);
    }

    bool ownsRealRef = false;
    IUnknown* realDevice = UnwrapDeviceForRealCall(pDevice, ownsRealRef);
    HRESULT hr = m_real2->CreateSwapChainForHwnd(realDevice, hWnd,
                                                  useDesc, pFullscreenDesc,
                                                  pRestrictToOutput, ppSwapChain);
    if (ownsRealRef && realDevice) realDevice->Release();
    NvDM_DxgiLog("    real CreateSwapChainForHwnd hr=0x%08lX  realSC=%p  logical=%ux%u  unwrapped=%d\n",
                 hr, ppSwapChain ? (void*)*ppSwapChain : nullptr, logicalW, logicalH, (int)ownsRealRef);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && g_pfnWrap)
    {
        void* wrapped = g_pfnWrap(pDevice, *ppSwapChain, logicalW, logicalH);
        if (wrapped)
        {
            *ppSwapChain = static_cast<IDXGISwapChain1*>(wrapped);
            NvDM_DxgiLog("    wrapped via d3d11.dll bridge -> %p\n", wrapped);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIFactoryProxy::CreateSwapChainForCoreWindow(
    IUnknown* pDevice, IUnknown* pWindow,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain)
{
    NvDM_DxgiLog("  IDXGIFactory2::CreateSwapChainForCoreWindow(pDevice=%p)\n", (void*)pDevice);
    if (!m_real2) return E_NOINTERFACE;
    if (!pDesc || !ppSwapChain)
        return m_real2->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);

    EnsureBridgeLoaded();

    unsigned int logicalW = pDesc->Width;
    unsigned int logicalH = pDesc->Height;
    DXGI_SWAP_CHAIN_DESC1 localDesc = *pDesc;
    const DXGI_SWAP_CHAIN_DESC1* useDesc = pDesc;
    if (g_pfnDoubleDesc1)
    {
        const void* doubled = g_pfnDoubleDesc1(&localDesc, &logicalW, &logicalH);
        if (doubled) useDesc = static_cast<const DXGI_SWAP_CHAIN_DESC1*>(doubled);
    }

    bool ownsRealRef = false;
    IUnknown* realDevice = UnwrapDeviceForRealCall(pDevice, ownsRealRef);
    HRESULT hr = m_real2->CreateSwapChainForCoreWindow(realDevice, pWindow,
                                                        useDesc, pRestrictToOutput, ppSwapChain);
    if (ownsRealRef && realDevice) realDevice->Release();
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && g_pfnWrap)
    {
        void* wrapped = g_pfnWrap(pDevice, *ppSwapChain, logicalW, logicalH);
        if (wrapped) *ppSwapChain = static_cast<IDXGISwapChain1*>(wrapped);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIFactoryProxy::CreateSwapChainForComposition(
    IUnknown* pDevice,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain)
{
    NvDM_DxgiLog("  IDXGIFactory2::CreateSwapChainForComposition(pDevice=%p)\n", (void*)pDevice);
    if (!m_real2) return E_NOINTERFACE;
    if (!pDesc || !ppSwapChain)
        return m_real2->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);

    EnsureBridgeLoaded();

    unsigned int logicalW = pDesc->Width;
    unsigned int logicalH = pDesc->Height;
    DXGI_SWAP_CHAIN_DESC1 localDesc = *pDesc;
    const DXGI_SWAP_CHAIN_DESC1* useDesc = pDesc;
    if (g_pfnDoubleDesc1)
    {
        const void* doubled = g_pfnDoubleDesc1(&localDesc, &logicalW, &logicalH);
        if (doubled) useDesc = static_cast<const DXGI_SWAP_CHAIN_DESC1*>(doubled);
    }

    bool ownsRealRef = false;
    IUnknown* realDevice = UnwrapDeviceForRealCall(pDevice, ownsRealRef);
    HRESULT hr = m_real2->CreateSwapChainForComposition(realDevice, useDesc,
                                                         pRestrictToOutput, ppSwapChain);
    if (ownsRealRef && realDevice) realDevice->Release();
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && g_pfnWrap)
    {
        void* wrapped = g_pfnWrap(pDevice, *ppSwapChain, logicalW, logicalH);
        if (wrapped) *ppSwapChain = static_cast<IDXGISwapChain1*>(wrapped);
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Public entry point used from dllmain.cpp
// ---------------------------------------------------------------------------
IUnknown* WrapRealFactoryAsRequested(IUnknown* realFactory, REFIID riid)
{
    if (!realFactory) return nullptr;

    // Try to QI for each version we support; any of them might be available.
    IDXGIFactory*  r0 = nullptr;
    IDXGIFactory1* r1 = nullptr;
    IDXGIFactory2* r2 = nullptr;
    realFactory->QueryInterface(IID_IDXGIFactory,  reinterpret_cast<void**>(&r0));
    realFactory->QueryInterface(IID_IDXGIFactory1, reinterpret_cast<void**>(&r1));
    realFactory->QueryInterface(IID_IDXGIFactory2, reinterpret_cast<void**>(&r2));
    if (!r0)
    {
        // Couldn't even get IDXGIFactory — bail. (Shouldn't happen for any
        // factory CreateDXGIFactory* returns.)
        if (r1) r1->Release();
        if (r2) r2->Release();
        NvDM_DxgiLog("  WrapRealFactoryAsRequested: real factory %p doesn't expose IDXGIFactory; not wrapping\n",
                     (void*)realFactory);
        return nullptr;
    }

    // Drop the original ref from the caller — DXGIFactoryProxy now owns the
    // ref chain (one each for r0/r1/r2 from our QI's above). The caller
    // released their ref by handing it to us through this function's
    // contract.
    realFactory->Release();

    auto* proxy = new DXGIFactoryProxy(r0, r1, r2);

    // Task #66: hot-patch the IDXGIAdapter / IDXGIAdapter1 vtables so the
    // GetDesc / GetDesc1 vendor spoof fires on real adapter pointers
    // (preserving struct layout — the previous wrap-the-adapter approach
    // crashed system d3d11.dll's internal device-creation walk). Idempotent
    // — only the first factory wrap actually patches.
    InstallAdapterVtablePatch(r0);

    // Cast as requested IID for return.
    if (riid == IID_IDXGIFactory2 && r2) return static_cast<IDXGIFactory2*>(proxy);
    if (riid == IID_IDXGIFactory1 && r1) return static_cast<IDXGIFactory1*>(proxy);
    return static_cast<IDXGIFactory*>(proxy);
}

} // namespace NvDirectMode

// Plain C-linkage shim so dllmain.cpp can call us without including
// DXGIFactoryProxy.h (which pulls dxgi.h, conflicting with dllmain's
// __declspec(dllexport) CreateDXGIFactory* declarations).
extern "C" void* NvDM_DXGI_WrapFactory(void* realFactoryUnknown, const IID* riidPtr)
{
    if (!realFactoryUnknown || !riidPtr) return nullptr;
    auto* real = static_cast<IUnknown*>(realFactoryUnknown);
    return NvDirectMode::WrapRealFactoryAsRequested(real, *riidPtr);
}
