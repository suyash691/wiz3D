#include "StdAfx.h"
#include "Device11Proxy.h"
#include "Context11Proxy.h"
#include "DXGIDeviceProxy.h"
#include "Texture2D11Proxy.h"
#include "RTV11Proxy.h"
#include "DSV11Proxy.h"
#include "StereoHeuristic.h"
#include "proxy_factory.h"     // for IID_wiz3D_Device11Proxy
#include "AdapterFunctions.h"  // DDILog

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

#pragma comment(lib, "dxguid.lib")

// Diagnostic macros — keep call sites mostly identical to NvDirectMode source
// so the port is a clean diff. Both route to wiz3D_proxy.log via DDILog.
#define LOG_VERBOSE(fmt, ...)         DDILog(fmt, ##__VA_ARGS__)
#define NVDM_TRACE_FIRST_N(n, fmt, ...) do { static int s_n = 0; if (s_n < (n)) { DDILog(fmt, ##__VA_ARGS__); ++s_n; } } while(0)

namespace wiz3d
{

Device11Proxy::Device11Proxy(ID3D11Device* real)
    : m_real(real)
    , m_ctxProxy(nullptr)
    , m_dxgiDeviceProxy(nullptr)
    , m_refs(1)
    , m_logicalWidth(0)
    , m_logicalHeight(0)
    , m_pBackBufferResource(nullptr)
{
    InitializeCriticalSection(&m_rtvSetLock);
    InitializeCriticalSection(&m_dxgiCacheLock);
}

Device11Proxy::~Device11Proxy()
{
    if (m_dxgiDeviceProxy)
    {
        // Detach parent first so any outstanding game-held refs on the
        // proxy can no longer call back into a destructed Device11Proxy
        // (their QI(ID3D11Device) returns E_NOINTERFACE).
        m_dxgiDeviceProxy->DetachParent();
        m_dxgiDeviceProxy->Release();
        m_dxgiDeviceProxy = nullptr;
    }
    DeleteCriticalSection(&m_rtvSetLock);
    DeleteCriticalSection(&m_dxgiCacheLock);
}

void Device11Proxy::RegisterBackBufferTexture(void* pTextureLike)
{
    m_pBackBufferResource = pTextureLike;
}

bool Device11Proxy::IsBackBufferResource(ID3D11Resource* p) const
{
    // ID3D11Texture2D -> ID3D11Resource is single-inheritance: same address.
    return p && static_cast<void*>(p) == m_pBackBufferResource;
}

void Device11Proxy::TrackBackBufferRTV(ID3D11RenderTargetView* rtv)
{
    if (!rtv) return;
    EnterCriticalSection(&m_rtvSetLock);
    m_backBufferRTVs.insert(rtv);
    LeaveCriticalSection(&m_rtvSetLock);
}

bool Device11Proxy::IsBackBufferRTV(ID3D11RenderTargetView* rtv) const
{
    if (!rtv) return false;
    // const_cast is fine here — only mutating the lock, the set is read-only
    // through this path.
    auto* self = const_cast<Device11Proxy*>(this);
    EnterCriticalSection(&self->m_rtvSetLock);
    bool found = m_backBufferRTVs.find(rtv) != m_backBufferRTVs.end();
    LeaveCriticalSection(&self->m_rtvSetLock);
    return found;
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateRenderTargetView(
    ID3D11Resource* pResource, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
    ID3D11RenderTargetView** ppRTView)
{
    // Stage 3a: pass through to the real device. The stereo right-eye RTV
    // creation requires knowing that pResource is one of our Texture2D11Proxy
    // instances — that reverse lookup needs the Device-level "wrapped-texture
    // -> proxy" map which lands in Stage 3b. Until then, this method keeps
    // its pre-existing BB-RTV tracking behaviour unchanged.
    HRESULT hr = m_real->CreateRenderTargetView(pResource, pDesc, ppRTView);
    if (SUCCEEDED(hr) && ppRTView && *ppRTView)
    {
        if (IsBackBufferResource(pResource))
        {
            TrackBackBufferRTV(*ppRTView);
            LOG_VERBOSE("  Device11Proxy::CreateRenderTargetView: BB-derived rtv=%p (resource=%p tracked)\n",
                        *ppRTView, pResource);
        }
        else
        {
            NVDM_TRACE_FIRST_N(8, "  Device11Proxy::CreateRenderTargetView: non-BB rtv=%p (resource=%p, our BB=%p)\n",
                               *ppRTView, pResource, m_pBackBufferResource);
        }
    }
    return hr;
}

void Device11Proxy::SetImmediateContextProxy(Context11Proxy* ctxProxy)
{
    m_ctxProxy = ctxProxy;
}

HRESULT STDMETHODCALLTYPE Device11Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ID3D11Device)
    {
        *ppvObj = static_cast<ID3D11Device*>(this);
        AddRef();
        return S_OK;
    }

    // Private IID for cross-DLL identification (dxgi.dll's factory wrap
    // QIs incoming `pDevice` to detect a Device11Proxy). Returned as an
    // IUnknown* — the caller is expected to cast back via known type.
    if (riid == IID_wiz3D_Device11Proxy)
    {
        *ppvObj = static_cast<IUnknown*>(static_cast<ID3D11Device*>(this));
        AddRef();
        return S_OK;
    }

    // Stage 2: IDXGIDevice family routes through DXGIDeviceProxy so the
    // game's `device->QI(IDXGIDevice) ... ->QI(ID3D11Device)` round-trip
    // returns to *this* Device11Proxy rather than the unwrapped real
    // device. Without this, refcount math diverges and the CRT free()
    // crashes when the second Release fires (Tomb Raider 2013 etc.).
    if (riid == IID_IDXGIDevice  ||
        riid == IID_IDXGIDevice1 ||
        riid == IID_IDXGIDevice2 ||
        riid == IID_IDXGIDevice3)
    {
        DXGIDeviceProxy* dp = GetOrCreateDXGIDeviceProxyAddRef();
        if (!dp) return E_NOINTERFACE;
        // Route through the proxy's QI to get the right interface cast,
        // which also AddRefs. Drop the AddRef we got from the cache helper.
        HRESULT hr = dp->QueryInterface(riid, ppvObj);
        dp->Release();
        return hr;
    }

    // Device1/Device2/Device3/etc. — pass through unwrapped for now.
    // 1b-iii/iv may need to claim those if a Direct Mode game uses them
    // for swap-chain / RTV creation.
    HRESULT hr = m_real->QueryInterface(riid, ppvObj);
    NVDM_TRACE_FIRST_N(16,
        "  Device11Proxy::QI(unknown IID, e.g. Device1+) hr=0x%08lX -- bypass risk\n", hr);
    return hr;
}

DXGIDeviceProxy* Device11Proxy::GetOrCreateDXGIDeviceProxyAddRef()
{
    EnterCriticalSection(&m_dxgiCacheLock);
    if (m_dxgiDeviceProxy)
    {
        m_dxgiDeviceProxy->AddRef();
        DXGIDeviceProxy* hit = m_dxgiDeviceProxy;
        LeaveCriticalSection(&m_dxgiCacheLock);
        return hit;
    }

    // Build the proxy by QI'ing each level on the real device. r0 is
    // mandatory; higher levels are best-effort.
    IDXGIDevice*  r0 = nullptr;
    IDXGIDevice1* r1 = nullptr;
    IDXGIDevice2* r2 = nullptr;
    IDXGIDevice3* r3 = nullptr;
    HRESULT hr0 = m_real->QueryInterface(IID_IDXGIDevice,  reinterpret_cast<void**>(&r0));
    if (FAILED(hr0) || !r0)
    {
        LeaveCriticalSection(&m_dxgiCacheLock);
        return nullptr;
    }
    m_real->QueryInterface(IID_IDXGIDevice1, reinterpret_cast<void**>(&r1));
    m_real->QueryInterface(IID_IDXGIDevice2, reinterpret_cast<void**>(&r2));
    m_real->QueryInterface(IID_IDXGIDevice3, reinterpret_cast<void**>(&r3));

    // ctor sets m_refs=1 — that's the cache's strong ref. AddRef once more
    // for the caller so we return with two outstanding refs (cache + caller).
    m_dxgiDeviceProxy = new DXGIDeviceProxy(this, r0, r1, r2, r3);
    m_dxgiDeviceProxy->AddRef();
    DXGIDeviceProxy* result = m_dxgiDeviceProxy;
    LeaveCriticalSection(&m_dxgiCacheLock);
    return result;
}

void STDMETHODCALLTYPE Device11Proxy::GetImmediateContext(ID3D11DeviceContext** ppImmediateContext)
{
    if (!ppImmediateContext) return;
    if (m_ctxProxy)
    {
        // Context11Proxy publicly inherits from ID3D11DeviceContext so this
        // is a real upcast — static_cast keeps /W4 + warnings-as-errors happy
        // (reinterpret_cast between related types triggers C4946).
        *ppImmediateContext = static_cast<ID3D11DeviceContext*>(m_ctxProxy);
        m_ctxProxy->AddRef();
        return;
    }
    // Fallback: game calls GetImmediateContext before we've stashed a proxy
    // (shouldn't happen via the standard D3D11CreateDevice path, but defensive).
    m_real->GetImmediateContext(ppImmediateContext);
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D)
{
    // Stage 3a: pure passthrough. The Texture2D11Proxy class is built and
    // ready (StereoHeuristic.h decides if a right-eye sibling is needed),
    // but ACTIVATING the wrapping requires the Stage 3b "private IID +
    // unwrap helper" pattern so that downstream methods (CreateRTV,
    // CreateSRV, CreateDSV, Context11Proxy::Copy*, etc.) can detect a
    // wrapped resource at their input and substitute the real underlying
    // pointer for the real D3D11 runtime. Without that unwrap, handing a
    // Texture2D11Proxy back to the game produces a guaranteed crash on the
    // very next call that passes it to the real runtime. So Stage 3a ships
    // the proxy classes as compiled-but-dormant infrastructure and Stage
    // 3b adds the IID + unwrap + activates the wrapping for real.
    return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateDepthStencilView(
    ID3D11Resource* pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    ID3D11DepthStencilView** ppDepthStencilView)
{
    HRESULT hr = m_real->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
    if (FAILED(hr) || !ppDepthStencilView || !*ppDepthStencilView) return hr;

    // Stage 3a: mirror the RTV stereo-sibling path for depth. The
    // pResource->IsStereo() lookup will land in Stage 3b once the
    // Texture2D11Proxy reverse map is wired up.
    NVDM_TRACE_FIRST_N(8, "  Device11Proxy::CreateDepthStencilView: dsv=%p (resource=%p)\n",
                       *ppDepthStencilView, pResource);
    return hr;
}

} // namespace wiz3d
