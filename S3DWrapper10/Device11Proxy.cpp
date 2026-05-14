#include "StdAfx.h"
#include "Device11Proxy.h"
#include "Context11Proxy.h"
#include "DXGIDeviceProxy.h"
#include "Texture2D11Proxy.h"
#include "RTV11Proxy.h"
#include "DSV11Proxy.h"
#include "Buffer11Proxy.h"
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
    // Stage 3b: probe pResource for a Texture2D11Proxy via the private IID.
    // On hit, unwrap to the real left-eye texture for the runtime's
    // CreateRTV; if the proxy also has a right-eye sibling, allocate a
    // matching right-eye RTV and bundle both into an RTV11Proxy returned
    // to the game.
    Texture2D11Proxy* texProxy   = TryUnwrapTexture2D(pResource);
    Buffer11Proxy*    bufProxy   = TryUnwrapBuffer(pResource);
    ID3D11Resource*   realToUse  = texProxy ? static_cast<ID3D11Resource*>(texProxy->GetReal())
                                 : bufProxy ? static_cast<ID3D11Resource*>(bufProxy->GetReal())
                                            : pResource;
    ID3D11Resource*   realRight  = texProxy ? static_cast<ID3D11Resource*>(texProxy->GetRealRight())
                                            : nullptr;

    HRESULT hr = m_real->CreateRenderTargetView(realToUse, pDesc, ppRTView);
    if (SUCCEEDED(hr) && ppRTView && *ppRTView)
    {
        // BB-RTV tracking still operates on the unwrapped game pointer so
        // the legacy back-buffer flow continues to work for shutdown logic.
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

        // Always wrap RTVs that came from a Texture2D11Proxy so identity
        // tracking flows through the wrapper. Right-eye sibling created only
        // if the underlying resource was stereo.
        if (texProxy)
        {
            ID3D11RenderTargetView* realRightRTV = nullptr;
            if (realRight)
            {
                HRESULT hr2 = m_real->CreateRenderTargetView(realRight, pDesc, &realRightRTV);
                if (FAILED(hr2)) realRightRTV = nullptr;   // fall through to mono RTV
            }
            auto* rtvProxy = new RTV11Proxy(*ppRTView, realRightRTV, this);
            *ppRTView = static_cast<ID3D11RenderTargetView*>(rtvProxy);
            NVDM_TRACE_FIRST_N(16,
                "  Device11Proxy::CreateRenderTargetView: -> RTV11Proxy=%p stereo=%d\n",
                rtvProxy, (int)rtvProxy->IsStereo());
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

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateBuffer(
    const D3D11_BUFFER_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Buffer** ppBuffer)
{
    // Stage 3c.1: always wrap so downstream Try*Unwrap calls see our proxy
    // identity even for buffers that aren't constant buffers. Buffers don't
    // get stereo doubling (no right-eye sibling) — the wrap is purely for
    // identity + the VS-binding tag bit Stage 4c.1 consults.
    HRESULT hr = m_real->CreateBuffer(pDesc, pInitialData, ppBuffer);
    if (FAILED(hr) || !ppBuffer || !*ppBuffer) return hr;
    auto* bufProxy = new Buffer11Proxy(*ppBuffer, this);
    *ppBuffer = static_cast<ID3D11Buffer*>(bufProxy);
    return hr;
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC* pDesc,
    const D3D11_SUBRESOURCE_DATA* pInitialData,
    ID3D11Texture2D** ppTexture2D)
{
    HRESULT hr = m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
    if (FAILED(hr) || !ppTexture2D || !*ppTexture2D) return hr;

    // Stage 3b: consult the heuristic on the just-allocated texture. If it
    // qualifies for stereo doubling, allocate a right-eye sibling via the
    // real device using the SAME desc but no initial data — the right-eye
    // is wiz3D's stereo replica and gets its content via per-eye writes in
    // Stage 4. Mirrors ResourceWrapper::CreateRightResource semantics.
    //
    // We always wrap, even for mono resources, so that:
    //   (a) GetDevice() returns our wrapped device (COM identity preserved)
    //   (b) TryUnwrapTexture2D detects our texture in downstream Create*View
    //       methods regardless of stereo status
    //   (c) Stage 4 can attach state-tracking metadata here later.
    SIZE bbSize = { (LONG)m_logicalWidth, (LONG)m_logicalHeight };
    const SIZE* pBBSize = (m_logicalWidth > 0 && m_logicalHeight > 0) ? &bbSize : nullptr;

    ID3D11Texture2D* realLeft  = *ppTexture2D;
    ID3D11Texture2D* realRight = nullptr;

    if (ShouldDoubleTexture2D(pDesc, pBBSize))
    {
        HRESULT hr2 = m_real->CreateTexture2D(pDesc, nullptr, &realRight);
        if (FAILED(hr2))
        {
            // Right-eye allocation failed (OOM, format restrictions, etc.).
            // Continue mono for this resource — game still works, just no
            // stereo for this RT.
            realRight = nullptr;
            NVDM_TRACE_FIRST_N(4, "  Device11Proxy::CreateTexture2D: right-eye alloc failed hr=0x%08lX (mono fallback)\n", hr2);
        }
    }

    auto* texProxy = new Texture2D11Proxy(realLeft, realRight, this);
    *ppTexture2D = static_cast<ID3D11Texture2D*>(texProxy);
    NVDM_TRACE_FIRST_N(32,
        "  Device11Proxy::CreateTexture2D: %ux%u fmt=%d bind=0x%X -> Texture2D11Proxy=%p stereo=%d\n",
        (unsigned)(pDesc ? pDesc->Width : 0),
        (unsigned)(pDesc ? pDesc->Height : 0),
        (int)(pDesc ? pDesc->Format : DXGI_FORMAT_UNKNOWN),
        (unsigned)(pDesc ? pDesc->BindFlags : 0),
        texProxy, (int)texProxy->IsStereo());
    return hr;
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateDepthStencilView(
    ID3D11Resource* pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    ID3D11DepthStencilView** ppDepthStencilView)
{
    // Stage 3b: same unwrap-then-rewrap pattern as CreateRenderTargetView.
    Texture2D11Proxy* texProxy   = TryUnwrapTexture2D(pResource);
    Buffer11Proxy*    bufProxy   = TryUnwrapBuffer(pResource);  // DSV-on-buffer is exotic but legal
    ID3D11Resource*   realToUse  = texProxy ? static_cast<ID3D11Resource*>(texProxy->GetReal())
                                 : bufProxy ? static_cast<ID3D11Resource*>(bufProxy->GetReal())
                                            : pResource;
    ID3D11Resource*   realRight  = texProxy ? static_cast<ID3D11Resource*>(texProxy->GetRealRight())
                                            : nullptr;

    HRESULT hr = m_real->CreateDepthStencilView(realToUse, pDesc, ppDepthStencilView);
    if (FAILED(hr) || !ppDepthStencilView || !*ppDepthStencilView) return hr;

    if (texProxy)
    {
        ID3D11DepthStencilView* realRightDSV = nullptr;
        if (realRight)
        {
            HRESULT hr2 = m_real->CreateDepthStencilView(realRight, pDesc, &realRightDSV);
            if (FAILED(hr2)) realRightDSV = nullptr;
        }
        auto* dsvProxy = new DSV11Proxy(*ppDepthStencilView, realRightDSV, this);
        *ppDepthStencilView = static_cast<ID3D11DepthStencilView*>(dsvProxy);
        NVDM_TRACE_FIRST_N(16,
            "  Device11Proxy::CreateDepthStencilView: -> DSV11Proxy=%p stereo=%d\n",
            dsvProxy, (int)dsvProxy->IsStereo());
    }
    else
    {
        NVDM_TRACE_FIRST_N(8, "  Device11Proxy::CreateDepthStencilView: dsv=%p (unwrapped resource=%p)\n",
                           *ppDepthStencilView, pResource);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateShaderResourceView(
    ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
    ID3D11ShaderResourceView** ppSRView)
{
    // Stage 3b: unwrap pResource only — Stage 3c will add an SRV11Proxy
    // class for full COM identity preservation. For now games get back an
    // unwrapped SRV against the real left-eye texture; per-eye SRV routing
    // is a Stage 4 concern.
    Texture2D11Proxy* texProxy  = TryUnwrapTexture2D(pResource);
    Buffer11Proxy*    bufProxy  = TryUnwrapBuffer(pResource);
    ID3D11Resource*   realToUse = texProxy ? static_cast<ID3D11Resource*>(texProxy->GetReal())
                                : bufProxy ? static_cast<ID3D11Resource*>(bufProxy->GetReal())
                                           : pResource;
    return m_real->CreateShaderResourceView(realToUse, pDesc, ppSRView);
}

HRESULT STDMETHODCALLTYPE Device11Proxy::CreateUnorderedAccessView(
    ID3D11Resource* pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    ID3D11UnorderedAccessView** ppUAView)
{
    // Stage 3b: unwrap pResource only — UAV11Proxy lands in Stage 3c.
    Texture2D11Proxy* texProxy  = TryUnwrapTexture2D(pResource);
    Buffer11Proxy*    bufProxy  = TryUnwrapBuffer(pResource);
    ID3D11Resource*   realToUse = texProxy ? static_cast<ID3D11Resource*>(texProxy->GetReal())
                                : bufProxy ? static_cast<ID3D11Resource*>(bufProxy->GetReal())
                                           : pResource;
    return m_real->CreateUnorderedAccessView(realToUse, pDesc, ppUAView);
}

} // namespace wiz3d
