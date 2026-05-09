#include "Device11Proxy.h"
#include "Context11Proxy.h"
#include "DXGIDeviceProxy.h"
#include "proxy_factory.h"   // for IID_NvDM_Device11Proxy
#include "log.h"

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

#pragma comment(lib, "dxguid.lib")

namespace NvDirectMode
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
    if (riid == IID_NvDM_Device11Proxy)
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
        *ppImmediateContext = reinterpret_cast<ID3D11DeviceContext*>(m_ctxProxy);
        m_ctxProxy->AddRef();
        return;
    }
    // Fallback: game calls GetImmediateContext before we've stashed a proxy
    // (shouldn't happen via the standard D3D11CreateDevice path, but defensive).
    m_real->GetImmediateContext(ppImmediateContext);
}

} // namespace NvDirectMode
