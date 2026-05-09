#include "SwapChainProxy.h"
#include "Device11Proxy.h"
#include "eye_state.h"
#include "log.h"

#pragma comment(lib, "dxguid.lib")  // for IID_IDXGISwapChain et al

// Output-mode + swap-eyes flags (from dllmain.cpp via the C-linkage bridge).
extern "C" int NvDM_OutputIsTopBottom();
extern "C" int NvDM_SwapEyes();

namespace NvDirectMode
{

SwapChainProxy::SwapChainProxy(IDXGISwapChain* real, Device11Proxy* parent)
    : m_real(real)
    , m_real1(nullptr)
    , m_parent(parent)
    , m_refs(1)
    , m_shadowBB(nullptr)
    , m_logicalW(0)
    , m_logicalH(0)
    , m_shadowFormat(DXGI_FORMAT_UNKNOWN)
{
    // QI for IDXGISwapChain1 best-effort. On Win10+ DXGI swap chains
    // always expose this; on Win7 without platform update it may fail.
    if (m_real)
        m_real->QueryInterface(IID_IDXGISwapChain1, reinterpret_cast<void**>(&m_real1));
}

SwapChainProxy::~SwapChainProxy()
{
    ReleaseShadowBB();
}

void SwapChainProxy::ReleaseShadowBB()
{
    if (m_shadowBB) { m_shadowBB->Release(); m_shadowBB = nullptr; }
    m_logicalW = 0;
    m_logicalH = 0;
    m_shadowFormat = DXGI_FORMAT_UNKNOWN;
}

void SwapChainProxy::EnsureShadowBB()
{
    if (m_shadowBB || !m_real || !m_parent) return;

    // Stage 3 v2: shadow allocated at the *logical* (one-eye) size.
    // Previous v1 doubled the shadow to provide per-eye viewport routing
    // inside one texture, but that broke games whose deferred rendering
    // queries the BB texture's GetDesc and sizes G-buffers / post-process
    // RTs accordingly — TR's "shaders broken with line down the middle"
    // was exactly this (game allocated 7680-wide G-buffer but our viewport
    // clamp restricted draws to the LEFT half, leaving the RIGHT half as
    // sampled garbage for the post-process pass).
    //
    // With a 1x shadow the game sees a normal-sized BB everywhere
    // (swap chain GetDesc, texture GetDesc, all consistent). For mono
    // games this Just Works. For genuine Direct Mode games the latest-
    // rendered eye lands in the shadow each frame and we blit it to the
    // real BB — a shutter-glasses-style flicker on a 2D display, but it
    // IS correct stereo output. Per-eye CAPTURE for proper SBS/T-B
    // composite is stage 4.
    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(m_real->GetDesc(&desc))) return;
    m_logicalW = desc.BufferDesc.Width;
    m_logicalH = desc.BufferDesc.Height;
    m_shadowFormat = desc.BufferDesc.Format;
    if (m_logicalW == 0 || m_logicalH == 0)
    {
        LOG_VERBOSE("  EnsureShadowBB: degenerate logical size %ux%u — skipping shadow alloc\n",
                    m_logicalW, m_logicalH);
        return;
    }

    m_parent->SetLogicalBackBufferSize(m_logicalW, m_logicalH);

    ID3D11Device* dev = m_parent->GetReal();
    if (!dev) return;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = m_logicalW;
    td.Height           = m_logicalH;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = m_shadowFormat;
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &m_shadowBB);
    if (FAILED(hr) || !m_shadowBB)
    {
        LOG_VERBOSE("  EnsureShadowBB: CreateTexture2D(%ux%u fmt=%d) FAILED hr=0x%08lX\n",
                    m_logicalW, m_logicalH, (int)m_shadowFormat, hr);
        m_shadowBB = nullptr;
        return;
    }
    LOG_VERBOSE("  EnsureShadowBB: shadow=%p (%ux%u 1x logical, fmt=%d)\n",
                m_shadowBB, m_logicalW, m_logicalH, (int)m_shadowFormat);
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_IDXGIObject ||
        riid == IID_IDXGIDeviceSubObject ||
        riid == IID_IDXGISwapChain)
    {
        *ppvObj = static_cast<IDXGISwapChain*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGISwapChain1 && m_real1)
    {
        *ppvObj = static_cast<IDXGISwapChain1*>(this);
        AddRef();
        return S_OK;
    }
    NVDM_TRACE_FIRST_N(8, "  SwapChainProxy::QI(unknown/higher IID) -> E_NOINTERFACE\n");
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
    NVDM_TRACE_FIRST_N(4, "  SwapChainProxy::Present(SyncInterval=%u, Flags=0x%X)\n", SyncInterval, Flags);
    BlitActiveEyeToRealBB();
    return m_real->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::Present1(UINT SyncInterval, UINT Flags,
                                                   const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    NVDM_TRACE_FIRST_N(4, "  SwapChainProxy::Present1(SyncInterval=%u, Flags=0x%X)\n", SyncInterval, Flags);
    if (!m_real1) return E_NOINTERFACE;
    BlitActiveEyeToRealBB();
    return m_real1->Present1(SyncInterval, Flags, pPresentParameters);
}

void SwapChainProxy::BlitActiveEyeToRealBB()
{
    if (!m_shadowBB || !m_real || !m_parent) return;

    ID3D11Texture2D* realBB = nullptr;
    HRESULT hr = m_real->GetBuffer(0, IID_ID3D11Texture2D,
                                   reinterpret_cast<void**>(&realBB));
    if (FAILED(hr) || !realBB) return;

    // 1x shadow → 1x real BB: full copy. For Direct Mode games the
    // shadow contains whichever eye was rendered most recently before
    // Present (game's natural alternation pattern). For mono games it's
    // just the normal mono frame.
    ID3D11DeviceContext* ctx = nullptr;
    if (m_parent->GetReal()) m_parent->GetReal()->GetImmediateContext(&ctx);
    if (ctx)
    {
        ctx->CopyResource(realBB, m_shadowBB);
        ctx->Release();
    }
    NVDM_TRACE_FIRST_N(2, "  BlitActiveEyeToRealBB: full copy shadow=%p -> realBB=%p (%ux%u)\n",
                       m_shadowBB, realBB, m_logicalW, m_logicalH);

    realBB->Release();
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    if (Buffer == 0)
    {
        EnsureShadowBB();
        if (m_shadowBB && ppSurface)
        {
            // Hand the game our shadow texture in place of the real BB.
            // QI for whatever interface flavour was requested
            // (ID3D11Texture2D / IDXGISurface / etc).
            HRESULT hr = m_shadowBB->QueryInterface(riid, ppSurface);
            if (SUCCEEDED(hr) && *ppSurface && m_parent)
            {
                m_parent->RegisterBackBufferTexture(*ppSurface);
                LOG_VERBOSE("  GetBuffer(0): handed shadow %p (as %p via QI) registered on dev=%p\n",
                            m_shadowBB, *ppSurface, m_parent);
            }
            else
            {
                NVDM_TRACE_FIRST_N(4, "  GetBuffer(0): shadow QI hr=0x%08lX  surface=%p\n",
                                   hr, ppSurface ? *ppSurface : nullptr);
            }
            return hr;
        }
    }
    HRESULT hr = m_real->GetBuffer(Buffer, riid, ppSurface);
    NVDM_TRACE_FIRST_N(4, "  GetBuffer(idx=%u) hr=0x%08lX surface=%p (passthrough — non-zero buffer)\n",
                       Buffer, hr, ppSurface ? *ppSurface : NULL);
    return hr;
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::ResizeBuffers(
    UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    // Game thinks the swap chain is at logical (one-eye) size so it asks
    // us to resize to W x H. Real swap chain agrees — pass straight
    // through. Shadow BB is dropped here; next GetBuffer(0) re-allocates
    // it at 2W x H against the new size.
    LOG_VERBOSE("  SwapChainProxy::ResizeBuffers(BufferCount=%u, %ux%u, fmt=%d, flags=0x%X)\n",
                BufferCount, Width, Height, (int)NewFormat, SwapChainFlags);
    ReleaseShadowBB();
    return m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

// NOTE: ResizeTarget / SetFullscreenState etc are inline passthroughs in
// the header — the real swap chain knows the same dimensions the game
// thinks it has now (no doubling), so no overrides needed.

} // namespace NvDirectMode
