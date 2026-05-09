#include "DXGIAdapterProxy.h"
#include "../spoof_identity.h"
#include "log.h"

#include <string.h>
#include <wchar.h>

#pragma comment(lib, "dxguid.lib")

namespace NvDirectMode
{

// ---------------------------------------------------------------------------
// dxgi.dll cross-DLL bridge — same pattern as DXGIFactoryProxy's bridge to
// d3d11.dll, just in the other direction. dxgi.dll's
// NvDM_DXGI_WrapFactory accepts a real IDXGIFactory and returns a wrapped
// IDXGIFactory* (or null if the dxgi proxy isn't running). We resolve it
// lazily on first use so games that don't have dxgi.dll dropped in still
// link cleanly — the call just returns null and we hand back the
// unwrapped factory.
// ---------------------------------------------------------------------------
namespace
{
    typedef void* (WINAPI *pfnNvDM_DXGI_WrapFactory)(void* realFactory, const IID* riidPtr);

    static pfnNvDM_DXGI_WrapFactory g_pfnWrapFactory = nullptr;
    static volatile LONG            g_dxgiBridgeProbed = 0;

    void EnsureDxgiBridge()
    {
        if (InterlockedCompareExchange(&g_dxgiBridgeProbed, 1, 0) != 0) return;
        HMODULE h = GetModuleHandleW(L"dxgi.dll");
        if (!h) return;
        g_pfnWrapFactory = (pfnNvDM_DXGI_WrapFactory)GetProcAddress(h, "NvDM_DXGI_WrapFactory");
        LOG_VERBOSE("  DXGIAdapterProxy bridge: dxgi.dll handle=%p  WrapFactory=%p\n",
                    h, (void*)g_pfnWrapFactory);
    }
}

DXGIAdapterProxy::DXGIAdapterProxy(IDXGIAdapter* r0, IDXGIAdapter1* r1)
    : m_real0(r0)
    , m_real1(r1)
    , m_refs(1)
{
    LOG_VERBOSE("  DXGIAdapterProxy ctor: real0=%p real1=%p\n", r0, r1);
}

DXGIAdapterProxy::~DXGIAdapterProxy()
{
    if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
    if (m_real0) { m_real0->Release(); m_real0 = nullptr; }
}

ULONG STDMETHODCALLTYPE DXGIAdapterProxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_IDXGIObject ||
        riid == IID_IDXGIAdapter)
    {
        *ppvObj = static_cast<IDXGIAdapter*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIAdapter1 && m_real1)
    {
        *ppvObj = static_cast<IDXGIAdapter1*>(this);
        AddRef();
        return S_OK;
    }
    // IDXGIAdapter2+ — same E_NOINTERFACE policy as DXGIFactoryProxy /
    // SwapChainProxy / Context11Proxy. Game falls back to a level we
    // wrap rather than escaping with an unwrapped pointer.
    NVDM_TRACE_FIRST_N(8, "  DXGIAdapterProxy::QI(unknown/higher IID) -> E_NOINTERFACE\n");
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

// ---------------------------------------------------------------------------
// Vendor spoof: rewrite DXGI_ADAPTER_DESC{,1} so games querying
// "what GPU is this?" via DXGI see the same NVIDIA RTX 2080 Ti identity
// NvApiProxy reports. Without this, games on AMD/Intel hardware see
// non-NVIDIA vendor IDs and skip loading nvapi.dll → stereo path never
// activates. Memory sizes / LUID / Flags are kept from the real adapter
// so other queries that don't matter for stereo (memory budget, etc.)
// stay accurate.
// ---------------------------------------------------------------------------
namespace {
    void ApplySpoofToDesc(DXGI_ADAPTER_DESC* d)
    {
        if (!d) return;
        // wcsncpy_s copies + null-terminates within the fixed-size buffer
        wcsncpy_s(d->Description,
                  sizeof(d->Description) / sizeof(WCHAR),
                  NvDirectMode::kSpoofGpuNameW,
                  _TRUNCATE);
        d->VendorId   = NvDirectMode::kSpoofPciVendor;
        d->DeviceId   = NvDirectMode::kSpoofPciDevice;
        d->SubSysId   = NvDirectMode::kSpoofSubSysId;
        d->Revision   = NvDirectMode::kSpoofRevision;
    }
    void ApplySpoofToDesc1(DXGI_ADAPTER_DESC1* d)
    {
        if (!d) return;
        wcsncpy_s(d->Description,
                  sizeof(d->Description) / sizeof(WCHAR),
                  NvDirectMode::kSpoofGpuNameW,
                  _TRUNCATE);
        d->VendorId = NvDirectMode::kSpoofPciVendor;
        d->DeviceId = NvDirectMode::kSpoofPciDevice;
        d->SubSysId = NvDirectMode::kSpoofSubSysId;
        d->Revision = NvDirectMode::kSpoofRevision;
        // Leave d->Flags alone (e.g. DXGI_ADAPTER_FLAG_SOFTWARE matters
        // for WARP detection — game might bail if we lie that a real
        // adapter is software).
    }
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::GetDesc(DXGI_ADAPTER_DESC* pDesc)
{
    HRESULT hr = m_real0->GetDesc(pDesc);
    if (SUCCEEDED(hr) && pDesc)
    {
        ApplySpoofToDesc(pDesc);
        NVDM_TRACE_FIRST_N(2, "  DXGIAdapterProxy::GetDesc: spoofed as NVIDIA RTX 2080 Ti (vendor=0x%04X)\n",
                           pDesc->VendorId);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::GetDesc1(DXGI_ADAPTER_DESC1* pDesc)
{
    if (!m_real1) return E_NOINTERFACE;
    HRESULT hr = m_real1->GetDesc1(pDesc);
    if (SUCCEEDED(hr) && pDesc)
    {
        ApplySpoofToDesc1(pDesc);
        NVDM_TRACE_FIRST_N(2, "  DXGIAdapterProxy::GetDesc1: spoofed as NVIDIA RTX 2080 Ti (vendor=0x%04X)\n",
                           pDesc->VendorId);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::GetParent(REFIID riid, void** ppParent)
{
    if (!ppParent) return E_POINTER;

    // For factory queries: get the real factory, hand it to dxgi.dll's
    // wrap bridge if available, and return the wrapped version. This is
    // the whole point of this class — keep the device→adapter→factory
    // walk inside our wrappers so games that bypass D3D11CreateDeviceAndSwapChain
    // still hit our DXGIFactoryProxy::CreateSwapChain* hooks.
    if (riid == IID_IDXGIFactory  || riid == IID_IDXGIFactory1 || riid == IID_IDXGIFactory2)
    {
        IUnknown* realFactory = nullptr;
        HRESULT hr = m_real0->GetParent(riid, reinterpret_cast<void**>(&realFactory));
        if (FAILED(hr) || !realFactory)
        {
            NVDM_TRACE_FIRST_N(2, "  DXGIAdapterProxy::GetParent(IDXGIFactory*) real failed hr=0x%08lX\n", hr);
            return hr;
        }
        EnsureDxgiBridge();
        if (g_pfnWrapFactory)
        {
            void* wrapped = g_pfnWrapFactory(realFactory, &riid);
            if (wrapped)
            {
                // Bridge consumed the real ref; wrapped holds the chain.
                *ppParent = wrapped;
                NVDM_TRACE_FIRST_N(4, "  DXGIAdapterProxy::GetParent: wrapped real=%p -> %p\n",
                                   (void*)realFactory, wrapped);
                return S_OK;
            }
            NVDM_TRACE_FIRST_N(2, "  DXGIAdapterProxy::GetParent: wrap returned null, falling through unwrapped\n");
        }
        // No dxgi.dll proxy loaded (or wrap returned null) — hand the
        // unwrapped factory to the game. We've already taken its ref,
        // so pass it through directly.
        *ppParent = realFactory;
        return S_OK;
    }

    // Other parent IIDs (rare) — passthrough.
    return m_real0->GetParent(riid, ppParent);
}

} // namespace NvDirectMode
