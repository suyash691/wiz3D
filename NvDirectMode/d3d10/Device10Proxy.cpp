#include "Device10Proxy.h"
#include "eye_state.h"
#include "log.h"

#pragma comment(lib, "dxguid.lib")

namespace NvDirectMode
{

Device10Proxy::Device10Proxy(ID3D10Device* real)
    : m_real(real)
    , m_refs(1)
    , m_logicalWidth(0)
    , m_logicalHeight(0)
    , m_pBackBufferResource(nullptr)
{
    InitializeCriticalSection(&m_rtvSetLock);
}

Device10Proxy::~Device10Proxy()
{
    DeleteCriticalSection(&m_rtvSetLock);
}

HRESULT STDMETHODCALLTYPE Device10Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ID3D10Device)
    {
        *ppvObj = static_cast<ID3D10Device*>(this);
        AddRef();
        return S_OK;
    }
    // ID3D10Device1 / IDXGIDevice etc — pass through unwrapped for now.
    HRESULT hr = m_real->QueryInterface(riid, ppvObj);
    NVDM_TRACE_FIRST_N(16,
        "  Device10Proxy::QI(unknown IID, e.g. Device1/IDXGIDevice) hr=0x%08lX -- bypass risk\n", hr);
    return hr;
}

void Device10Proxy::RegisterBackBufferTexture(void* pTextureLike)
{
    m_pBackBufferResource = pTextureLike;
}

bool Device10Proxy::IsBackBufferResource(ID3D10Resource* p) const
{
    return p && static_cast<void*>(p) == m_pBackBufferResource;
}

void Device10Proxy::TrackBackBufferRTV(ID3D10RenderTargetView* rtv)
{
    if (!rtv) return;
    EnterCriticalSection(&m_rtvSetLock);
    m_backBufferRTVs.insert(rtv);
    LeaveCriticalSection(&m_rtvSetLock);
}

bool Device10Proxy::IsBackBufferRTV(ID3D10RenderTargetView* rtv) const
{
    if (!rtv) return false;
    auto* self = const_cast<Device10Proxy*>(this);
    EnterCriticalSection(&self->m_rtvSetLock);
    bool found = m_backBufferRTVs.find(rtv) != m_backBufferRTVs.end();
    LeaveCriticalSection(&self->m_rtvSetLock);
    return found;
}

HRESULT STDMETHODCALLTYPE Device10Proxy::CreateRenderTargetView(
    ID3D10Resource* pResource, const D3D10_RENDER_TARGET_VIEW_DESC* pDesc,
    ID3D10RenderTargetView** ppRTView)
{
    HRESULT hr = m_real->CreateRenderTargetView(pResource, pDesc, ppRTView);
    if (SUCCEEDED(hr) && ppRTView && *ppRTView)
    {
        if (IsBackBufferResource(pResource))
        {
            TrackBackBufferRTV(*ppRTView);
            LOG_VERBOSE("  Device10Proxy::CreateRenderTargetView: BB-derived rtv=%p (resource=%p tracked)\n",
                        *ppRTView, pResource);
        }
        else
        {
            NVDM_TRACE_FIRST_N(8, "  Device10Proxy::CreateRenderTargetView: non-BB rtv=%p (resource=%p, our BB=%p)\n",
                               *ppRTView, pResource, m_pBackBufferResource);
        }
    }
    return hr;
}

void STDMETHODCALLTYPE Device10Proxy::OMSetRenderTargets(
    UINT NumViews, ID3D10RenderTargetView* const* ppRenderTargetViews,
    ID3D10DepthStencilView* pDepthStencilView)
{
    m_real->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
    // Stage 3 v2 / shadow-RT: shadow texture is at logical (1x) size, so
    // no per-eye viewport clamp needed any more. Game's natural rendering
    // goes straight into the shadow at the size the game expects. The
    // BB-RTV identity tracking still happens (for diagnostic logging and
    // future per-eye texture-region routing if needed).
    if (NumViews > 0 && ppRenderTargetViews && IsBackBufferRTV(ppRenderTargetViews[0]))
        NVDM_TRACE_FIRST_N(8, "  Device10Proxy::OMSet BB-RTV bound rtv=%p (no clamp \xe2\x80\x94 shadow is 1x)\n",
                           ppRenderTargetViews[0]);
}

} // namespace NvDirectMode
