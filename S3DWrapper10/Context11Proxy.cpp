/* wiz3D - ID3D11DeviceContext proxy implementation (Option B Stage 2)
 *
 * Pure passthrough port of NvDirectMode/d3d11/Context11Proxy. The stage-3 BB
 * tracking and stage-4 magic-header capture were stripped for the MVP — the
 * job here is to prove COM identity + refcounting are right, not to do any
 * stereo work yet. OMSet/RSSetViewports/CopyResource/CopySubresourceRegion
 * are forwarded unchanged; per-eye behaviour will be re-added in Stage 4.
 */

#include "StdAfx.h"
#include "Context11Proxy.h"
#include "Device11Proxy.h"
#include "Texture2D11Proxy.h"
#include "RTV11Proxy.h"
#include "DSV11Proxy.h"
#include "proxy_factory.h"     // TryUnwrap* helpers
#include "AdapterFunctions.h"  // DDILog

// Static-size cap on per-call temp arrays used to unwrap RTV/RSV pointer
// arrays passed to OMSetRenderTargets. D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT
// is 8; UAVs go higher but we cap defensively.
static constexpr UINT kMaxUnwrapArray = 16;

#pragma comment(lib, "dxguid.lib")

namespace wiz3d
{

Context11Proxy::Context11Proxy(ID3D11DeviceContext* real, Device11Proxy* parent)
    : m_real(real)
    , m_parent(parent)
    , m_refs(1)
    , m_currentBBBound(false)
{
}

Context11Proxy::~Context11Proxy() = default;

HRESULT STDMETHODCALLTYPE Context11Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_ID3D11DeviceChild ||
        riid == IID_ID3D11DeviceContext)
    {
        *ppvObj = static_cast<ID3D11DeviceContext*>(this);
        AddRef();
        return S_OK;
    }
    // Context1+ family: refuse so games fall back to the wrapped base
    // interface instead of getting an unwrapped escape hatch.
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

void STDMETHODCALLTYPE Context11Proxy::OMSetRenderTargets(
    UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews,
    ID3D11DepthStencilView* pDepthStencilView)
{
    // Stage 3b: unwrap RTV array + DSV to the real (left-eye) views before
    // forwarding. Per-eye dispatch lives in Stage 4 — for now everything
    // binds left-only so the game renders mono via our COM proxies.
    ID3D11RenderTargetView* realRTVs[kMaxUnwrapArray] = { 0 };
    ID3D11RenderTargetView* const* rtvsToUse = ppRenderTargetViews;
    if (NumViews > 0 && ppRenderTargetViews)
    {
        UINT cap = NumViews <= kMaxUnwrapArray ? NumViews : kMaxUnwrapArray;
        for (UINT i = 0; i < cap; ++i)
        {
            RTV11Proxy* p = TryUnwrapRTV(ppRenderTargetViews[i]);
            realRTVs[i] = p ? p->GetReal() : ppRenderTargetViews[i];
        }
        rtvsToUse = realRTVs;
    }
    ID3D11DepthStencilView* realDSV = pDepthStencilView;
    if (DSV11Proxy* d = TryUnwrapDSV(pDepthStencilView)) realDSV = d->GetReal();
    m_real->OMSetRenderTargets(NumViews, rtvsToUse, realDSV);
}

void STDMETHODCALLTYPE Context11Proxy::OMSetRenderTargetsAndUnorderedAccessViews(
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews,
    ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs,
    ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    const UINT* pUAVInitialCounts)
{
    ID3D11RenderTargetView* realRTVs[kMaxUnwrapArray] = { 0 };
    ID3D11RenderTargetView* const* rtvsToUse = ppRenderTargetViews;
    if (NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL &&
        NumRTVs > 0 && ppRenderTargetViews)
    {
        UINT cap = NumRTVs <= kMaxUnwrapArray ? NumRTVs : kMaxUnwrapArray;
        for (UINT i = 0; i < cap; ++i)
        {
            RTV11Proxy* p = TryUnwrapRTV(ppRenderTargetViews[i]);
            realRTVs[i] = p ? p->GetReal() : ppRenderTargetViews[i];
        }
        rtvsToUse = realRTVs;
    }
    ID3D11DepthStencilView* realDSV = pDepthStencilView;
    if (NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
    {
        if (DSV11Proxy* d = TryUnwrapDSV(pDepthStencilView)) realDSV = d->GetReal();
    }
    // UAVs not yet wrapped (Stage 3c). Pass through unchanged.
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(
        NumRTVs, rtvsToUse, realDSV,
        UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}

void STDMETHODCALLTYPE Context11Proxy::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports)
{
    m_real->RSSetViewports(NumViewports, pViewports);
}

void STDMETHODCALLTYPE Context11Proxy::CopyResource(
    ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource)
{
    // Stage 3b: unwrap both endpoints. Per-eye copy routing (also copying
    // src.right to dst.right when both are stereo) is a Stage 4 concern.
    Texture2D11Proxy* dst = TryUnwrapTexture2D(pDstResource);
    Texture2D11Proxy* src = TryUnwrapTexture2D(pSrcResource);
    m_real->CopyResource(dst ? dst->GetReal() : pDstResource,
                          src ? src->GetReal() : pSrcResource);
}

void STDMETHODCALLTYPE Context11Proxy::CopySubresourceRegion(
    ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
    UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource,
    const D3D11_BOX* pSrcBox)
{
    Texture2D11Proxy* dst = TryUnwrapTexture2D(pDstResource);
    Texture2D11Proxy* src = TryUnwrapTexture2D(pSrcResource);
    m_real->CopySubresourceRegion(dst ? dst->GetReal() : pDstResource, DstSubresource, DstX, DstY, DstZ,
                                  src ? src->GetReal() : pSrcResource, SrcSubresource, pSrcBox);
}

HRESULT STDMETHODCALLTYPE Context11Proxy::Map(
    ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    Texture2D11Proxy* tex = TryUnwrapTexture2D(pResource);
    return m_real->Map(tex ? tex->GetReal() : pResource, Subresource, MapType, MapFlags, pMappedResource);
}

void STDMETHODCALLTYPE Context11Proxy::Unmap(ID3D11Resource* pResource, UINT Subresource)
{
    Texture2D11Proxy* tex = TryUnwrapTexture2D(pResource);
    m_real->Unmap(tex ? tex->GetReal() : pResource, Subresource);
}

void STDMETHODCALLTYPE Context11Proxy::UpdateSubresource(
    ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox,
    const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    Texture2D11Proxy* tex = TryUnwrapTexture2D(pDstResource);
    m_real->UpdateSubresource(tex ? tex->GetReal() : pDstResource,
                              DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void STDMETHODCALLTYPE Context11Proxy::ResolveSubresource(
    ID3D11Resource* pDstResource, UINT DstSubresource,
    ID3D11Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
    Texture2D11Proxy* dst = TryUnwrapTexture2D(pDstResource);
    Texture2D11Proxy* src = TryUnwrapTexture2D(pSrcResource);
    m_real->ResolveSubresource(dst ? dst->GetReal() : pDstResource, DstSubresource,
                                src ? src->GetReal() : pSrcResource, SrcSubresource, Format);
}

void STDMETHODCALLTYPE Context11Proxy::ClearRenderTargetView(
    ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4])
{
    RTV11Proxy* rtv = TryUnwrapRTV(pRenderTargetView);
    m_real->ClearRenderTargetView(rtv ? rtv->GetReal() : pRenderTargetView, ColorRGBA);
}

void STDMETHODCALLTYPE Context11Proxy::ClearDepthStencilView(
    ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    DSV11Proxy* dsv = TryUnwrapDSV(pDepthStencilView);
    m_real->ClearDepthStencilView(dsv ? dsv->GetReal() : pDepthStencilView, ClearFlags, Depth, Stencil);
}

void STDMETHODCALLTYPE Context11Proxy::GetDevice(ID3D11Device** ppDevice)
{
    // COM identity: GetDevice must return the wrapped device, not the real
    // one — otherwise a game that round-trips through GetDevice ends up
    // bypassing our wrapper for subsequent resource creation.
    if (!ppDevice) return;
    if (m_parent)
    {
        // Device11Proxy publicly inherits from ID3D11Device — real upcast,
        // static_cast keeps /W4 + warnings-as-errors happy.
        *ppDevice = static_cast<ID3D11Device*>(m_parent);
        m_parent->AddRef();
        return;
    }
    m_real->GetDevice(ppDevice);
}

} // namespace wiz3d
