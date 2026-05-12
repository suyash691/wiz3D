#include "Device10Proxy.h"
#include "SwapChainProxy.h"
#include "magic_header_capture.h"
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

// ---------------------------------------------------------------------------
// Magic-header SBS capture intercepts (CopyResource / CopySubresourceRegion).
// Detects (2W × H+1) Texture2D sources tagged with NVSTEREO_IMAGE_SIGNATURE
// being copied to the swap-chain backbuffer, splits them into per-eye via
// SwapChainProxy::CaptureMagicHeaderSBS, and skips the real blit so our
// OutputMethod composite can write the final stereo frame to the BB.
// ---------------------------------------------------------------------------
namespace
{
    // Bounded cache of surface pointers we've already verified as magic-tagged.
    // Same pattern as the d3d9 path: avoids the per-call staging-copy probe
    // once we've seen a texture produce the magic. Surface pointers can be
    // reused after Release, but the worst case is a single extra staging
    // probe on the recycled pointer.
    constexpr size_t kMaxKnownStereoTex = 16;
    ID3D10Resource* g_knownStereoTex[kMaxKnownStereoTex] = {};
    UINT            g_knownStereoCount = 0;
    bool            g_knownStereoSwap  = false;  // last detected swap flag

    bool IsKnown(ID3D10Resource* p)
    {
        for (UINT i = 0; i < g_knownStereoCount; ++i)
            if (g_knownStereoTex[i] == p) return true;
        return false;
    }
    void MarkKnown(ID3D10Resource* p)
    {
        if (IsKnown(p)) return;
        if (g_knownStereoCount < kMaxKnownStereoTex)
            g_knownStereoTex[g_knownStereoCount++] = p;
    }

    // Returns true if we handled this copy via the magic-header path.
    bool TryMagicHeaderCapture(ID3D10Device* dev, Device10Proxy* parent,
                               ID3D10Resource* dst, ID3D10Resource* src)
    {
        if (!parent->IsBackBufferResource(dst)) return false;
        if (!src) return false;

        SwapChainProxy* sc = SwapChainProxy::GetPrimary();
        if (!sc) return false;

        UINT eyeW = 0, eyeH = 0;
        bool swap = false;
        if (IsKnown(src))
        {
            // Fast path: pull dims from the texture desc.
            ID3D10Texture2D* t = nullptr;
            if (FAILED(src->QueryInterface(__uuidof(ID3D10Texture2D), (void**)&t)) || !t)
                return false;
            D3D10_TEXTURE2D_DESC d = {};
            t->GetDesc(&d);
            t->Release();
            if (d.Width < 4 || (d.Width & 1) || d.Height < 2) return false;
            eyeW = d.Width / 2;
            eyeH = d.Height - 1;
            swap = g_knownStereoSwap;
        }
        else
        {
            MagicHeader::DetectResult r = MagicHeader::DetectStereoMagic(dev, src);
            if (!r.hasMagic) return false;
            MarkKnown(src);
            g_knownStereoSwap = r.swapEyes;
            eyeW = r.eyeWidth;
            eyeH = r.eyeHeight;
            swap = r.swapEyes;
            LOG_VERBOSE("  d3d10 CopyResource: magic-header SBS detected (src=%p, %ux%u eye)\n",
                        (void*)src, eyeW, eyeH);
        }

        sc->CaptureMagicHeaderSBS(src, eyeW, eyeH, swap);
        return true;
    }
}

void STDMETHODCALLTYPE Device10Proxy::CopyResource(
    ID3D10Resource* pDstResource, ID3D10Resource* pSrcResource)
{
    if (TryMagicHeaderCapture(m_real, this, pDstResource, pSrcResource))
        return;
    m_real->CopyResource(pDstResource, pSrcResource);
}

void STDMETHODCALLTYPE Device10Proxy::CopySubresourceRegion(
    ID3D10Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
    UINT DstZ, ID3D10Resource* pSrcResource, UINT SrcSubresource,
    const D3D10_BOX* pSrcBox)
{
    // Some producers use CopySubresourceRegion (with null pSrcBox + zero
    // dst offsets) instead of CopyResource for the final blit. Honour that
    // case too — anything else (non-zero dst offset, sub-region copy) is
    // not the stereo-blit-to-backbuffer pattern and falls through.
    if (DstSubresource == 0 && DstX == 0 && DstY == 0 && DstZ == 0 &&
        SrcSubresource == 0 && pSrcBox == nullptr &&
        TryMagicHeaderCapture(m_real, this, pDstResource, pSrcResource))
    {
        return;
    }
    m_real->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ,
                                  pSrcResource, SrcSubresource, pSrcBox);
}

} // namespace NvDirectMode
