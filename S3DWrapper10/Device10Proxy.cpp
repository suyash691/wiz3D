/* wiz3D - ID3D10Device proxy implementation (Option B for DX10) */

#include "StdAfx.h"
#include "Device10Proxy.h"
#include "Texture2D10Proxy.h"
#include "RTV10Proxy.h"
#include "DSV10Proxy.h"
#include "Buffer10Proxy.h"
#include "DXGIDevice10Proxy.h"
#include "StereoHeuristic.h"
#include "proxy_factory.h"     // IID_wiz3D_Device10Proxy + TryUnwrap_10 helpers
#include "AdapterFunctions.h"  // DDILog

#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

#pragma comment(lib, "dxguid.lib")

// Defensive cap on stack arrays used to unwrap CB / VB / RTV pointer arrays.
// D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT = 8, CB slots are 14; 16 covers it.
static constexpr UINT kMaxUnwrapArray = 16;

namespace wiz3d
{

// Forward UnwrapBuf10 helper — same shape as the DX11 UnwrapBuf in
// Context11Proxy.cpp. Passing a Buffer10Proxy directly to D3D10 would crash
// because the runtime doesn't understand our vtable past ID3D10Buffer.
static inline ID3D10Buffer* UnwrapBuf10(ID3D10Buffer* p)
{
    if (!p) return nullptr;
    if (auto* bp = TryUnwrapBuffer_10(static_cast<ID3D10Resource*>(p)))
        return bp->GetReal();
    return p;
}

// Eye-aware resource unwrap. Texture proxies pick left vs right based on
// the active eye; Buffer proxies have no eye distinction (single real).
static inline ID3D10Resource* UnwrapResForEye10(ID3D10Resource* p, bool pickRight)
{
    if (!p) return nullptr;
    if (auto* tex = TryUnwrapTexture2D_10(p))
    {
        ID3D10Resource* right = tex->GetRealRight();
        return (pickRight && right) ? right : static_cast<ID3D10Resource*>(tex->GetReal());
    }
    if (auto* buf = TryUnwrapBuffer_10(p))
        return static_cast<ID3D10Resource*>(buf->GetReal());
    return p;
}

Device10Proxy::Device10Proxy(ID3D10Device* real)
    : m_real(real)
    , m_refs(1)
    , m_activeEye(Eye::Left)
    , m_logicalWidth(0)
    , m_logicalHeight(0)
    , m_presentHookActive(false)
    , m_dxgiDeviceProxy(nullptr)
{
    InitializeCriticalSection(&m_dxgiCacheLock);
}

void Device10Proxy::ClearFrameCommands()
{
    m_frameCommands.clear();
}

void Device10Proxy::ReplayFrameCommands(Eye eye)
{
    Eye saved = m_activeEye;
    m_activeEye = eye;
    for (auto& fn : m_frameCommands) fn();
    m_activeEye = saved;
}

Device10Proxy::~Device10Proxy()
{
    // Sever the back-pointer first so any outstanding game-held refs on
    // the DXGI proxy don't route QI(ID3D10Device) through a destructed
    // parent. Then drop our cache ref — the proxy self-deletes if no
    // other refs exist.
    if (m_dxgiDeviceProxy)
    {
        m_dxgiDeviceProxy->DetachParent();
        m_dxgiDeviceProxy->Release();
        m_dxgiDeviceProxy = nullptr;
    }
    DeleteCriticalSection(&m_dxgiCacheLock);
}

DXGIDevice10Proxy* Device10Proxy::GetOrCreateDXGIDeviceProxyAddRef()
{
    EnterCriticalSection(&m_dxgiCacheLock);
    if (m_dxgiDeviceProxy)
    {
        m_dxgiDeviceProxy->AddRef();
        DXGIDevice10Proxy* hit = m_dxgiDeviceProxy;
        LeaveCriticalSection(&m_dxgiCacheLock);
        return hit;
    }

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

    // ctor sets m_refs=1 (cache's ref); AddRef again for caller.
    m_dxgiDeviceProxy = new DXGIDevice10Proxy(this, r0, r1, r2, r3);
    m_dxgiDeviceProxy->AddRef();
    DXGIDevice10Proxy* hit = m_dxgiDeviceProxy;
    LeaveCriticalSection(&m_dxgiCacheLock);
    return hit;
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
    // Private IID for cross-DLL identity (DXGIFactoryWrapper's Option B
    // factory hook QIs incoming pDevice to detect a Device10Proxy).
    if (riid == IID_wiz3D_Device10Proxy)
    {
        *ppvObj = static_cast<IUnknown*>(static_cast<ID3D10Device*>(this));
        AddRef();
        return S_OK;
    }
    // IDXGIDevice family: route through DXGIDevice10Proxy so the game's
    // `dev->QI(IDXGIDevice)->QI(ID3D10Device)` round-trip lands on this
    // Device10Proxy. Without this, the runtime passes the unwrapped real
    // device to factory->CreateSwapChain and our factory hook misses.
    if (riid == IID_IDXGIDevice  ||
        riid == IID_IDXGIDevice1 ||
        riid == IID_IDXGIDevice2 ||
        riid == IID_IDXGIDevice3)
    {
        DXGIDevice10Proxy* dp = GetOrCreateDXGIDeviceProxyAddRef();
        if (!dp) return E_NOINTERFACE;
        HRESULT hr = dp->QueryInterface(riid, ppvObj);
        dp->Release();
        return hr;
    }
    // ID3D10Device1 + vendor IIDs — pass through unwrapped for now.
    return m_real->QueryInterface(riid, ppvObj);
}

// ---------------------------------------------------------------------------
// CreateXxx — wrap returned resources/views so downstream Try*Unwrap_10
// picks them up at the COM boundary.
// ---------------------------------------------------------------------------
HRESULT STDMETHODCALLTYPE Device10Proxy::CreateBuffer(
    const D3D10_BUFFER_DESC* pDesc,
    const D3D10_SUBRESOURCE_DATA* pInitialData,
    ID3D10Buffer** ppBuffer)
{
    HRESULT hr = m_real->CreateBuffer(pDesc, pInitialData, ppBuffer);
    if (FAILED(hr) || !ppBuffer || !*ppBuffer) return hr;
    auto* bufProxy = new Buffer10Proxy(*ppBuffer, this);
    *ppBuffer = static_cast<ID3D10Buffer*>(bufProxy);
    return hr;
}

HRESULT STDMETHODCALLTYPE Device10Proxy::CreateTexture2D(
    const D3D10_TEXTURE2D_DESC* pDesc,
    const D3D10_SUBRESOURCE_DATA* pInitialData,
    ID3D10Texture2D** ppTexture2D)
{
    HRESULT hr = m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
    if (FAILED(hr) || !ppTexture2D || !*ppTexture2D) return hr;

    SIZE bbSize = { (LONG)m_logicalWidth, (LONG)m_logicalHeight };
    const SIZE* pBBSize = (m_logicalWidth > 0 && m_logicalHeight > 0) ? &bbSize : nullptr;

    ID3D10Texture2D* realLeft  = *ppTexture2D;
    ID3D10Texture2D* realRight = nullptr;
    if (ShouldDoubleTexture2D(pDesc, pBBSize))
    {
        HRESULT hr2 = m_real->CreateTexture2D(pDesc, nullptr, &realRight);
        if (FAILED(hr2)) realRight = nullptr;
    }

    auto* texProxy = new Texture2D10Proxy(realLeft, realRight, this);
    *ppTexture2D = static_cast<ID3D10Texture2D*>(texProxy);
    return hr;
}

HRESULT STDMETHODCALLTYPE Device10Proxy::CreateRenderTargetView(
    ID3D10Resource* pResource, const D3D10_RENDER_TARGET_VIEW_DESC* pDesc,
    ID3D10RenderTargetView** ppRTView)
{
    Texture2D10Proxy* texProxy  = TryUnwrapTexture2D_10(pResource);
    Buffer10Proxy*    bufProxy  = TryUnwrapBuffer_10(pResource);
    ID3D10Resource*   realLeft  = texProxy ? static_cast<ID3D10Resource*>(texProxy->GetReal())
                                : bufProxy ? static_cast<ID3D10Resource*>(bufProxy->GetReal())
                                           : pResource;
    ID3D10Resource*   realRight = texProxy ? static_cast<ID3D10Resource*>(texProxy->GetRealRight())
                                           : nullptr;

    HRESULT hr = m_real->CreateRenderTargetView(realLeft, pDesc, ppRTView);
    if (FAILED(hr) || !ppRTView || !*ppRTView) return hr;

    if (texProxy)
    {
        ID3D10RenderTargetView* realRightRTV = nullptr;
        if (realRight)
        {
            if (FAILED(m_real->CreateRenderTargetView(realRight, pDesc, &realRightRTV)))
                realRightRTV = nullptr;
        }
        auto* rtvProxy = new RTV10Proxy(*ppRTView, realRightRTV, this);
        *ppRTView = static_cast<ID3D10RenderTargetView*>(rtvProxy);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Device10Proxy::CreateDepthStencilView(
    ID3D10Resource* pResource, const D3D10_DEPTH_STENCIL_VIEW_DESC* pDesc,
    ID3D10DepthStencilView** ppDepthStencilView)
{
    Texture2D10Proxy* texProxy  = TryUnwrapTexture2D_10(pResource);
    Buffer10Proxy*    bufProxy  = TryUnwrapBuffer_10(pResource);
    ID3D10Resource*   realLeft  = texProxy ? static_cast<ID3D10Resource*>(texProxy->GetReal())
                                : bufProxy ? static_cast<ID3D10Resource*>(bufProxy->GetReal())
                                           : pResource;
    ID3D10Resource*   realRight = texProxy ? static_cast<ID3D10Resource*>(texProxy->GetRealRight())
                                           : nullptr;

    HRESULT hr = m_real->CreateDepthStencilView(realLeft, pDesc, ppDepthStencilView);
    if (FAILED(hr) || !ppDepthStencilView || !*ppDepthStencilView) return hr;

    if (texProxy)
    {
        ID3D10DepthStencilView* realRightDSV = nullptr;
        if (realRight)
        {
            if (FAILED(m_real->CreateDepthStencilView(realRight, pDesc, &realRightDSV)))
                realRightDSV = nullptr;
        }
        auto* dsvProxy = new DSV10Proxy(*ppDepthStencilView, realRightDSV, this);
        *ppDepthStencilView = static_cast<ID3D10DepthStencilView*>(dsvProxy);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Device10Proxy::CreateShaderResourceView(
    ID3D10Resource* pResource, const D3D10_SHADER_RESOURCE_VIEW_DESC* pDesc,
    ID3D10ShaderResourceView** ppSRView)
{
    Texture2D10Proxy* texProxy  = TryUnwrapTexture2D_10(pResource);
    Buffer10Proxy*    bufProxy  = TryUnwrapBuffer_10(pResource);
    ID3D10Resource*   realToUse = texProxy ? static_cast<ID3D10Resource*>(texProxy->GetReal())
                                : bufProxy ? static_cast<ID3D10Resource*>(bufProxy->GetReal())
                                           : pResource;
    return m_real->CreateShaderResourceView(realToUse, pDesc, ppSRView);
}

// ---------------------------------------------------------------------------
// *SetConstantBuffers — unwrap + VS-pipeline tag (Stage 4c.1 carryover).
// ---------------------------------------------------------------------------
#define D3D10_CB_SET(STAGE_PREFIX, IS_VS_PIPELINE)                                          \
void STDMETHODCALLTYPE Device10Proxy::STAGE_PREFIX##SetConstantBuffers(                     \
    UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers)                \
{                                                                                           \
    ID3D10Buffer* raw[kMaxUnwrapArray] = { 0 };                                             \
    UINT cap = NumBuffers <= kMaxUnwrapArray ? NumBuffers : kMaxUnwrapArray;                \
    for (UINT i = 0; i < cap; ++i)                                                          \
    {                                                                                       \
        ID3D10Buffer* p = ppConstantBuffers ? ppConstantBuffers[i] : nullptr;               \
        if (p)                                                                              \
        {                                                                                   \
            if (auto* bp = TryUnwrapBuffer_10(static_cast<ID3D10Resource*>(p)))             \
            {                                                                               \
                if (IS_VS_PIPELINE) bp->TagVSBound();                                       \
                raw[i] = bp->GetReal();                                                     \
            }                                                                               \
            else                                                                            \
            {                                                                               \
                raw[i] = p;                                                                 \
            }                                                                               \
        }                                                                                   \
    }                                                                                       \
    m_real->STAGE_PREFIX##SetConstantBuffers(StartSlot, NumBuffers,                         \
        ppConstantBuffers ? raw : nullptr);                                                 \
    if (!m_presentHookActive) return;                                                       \
    std::vector<ComRefHolder10> refs;                                                       \
    refs.reserve(NumBuffers);                                                               \
    for (UINT i = 0; i < NumBuffers; ++i)                                                   \
        refs.emplace_back(ppConstantBuffers ? ppConstantBuffers[i] : nullptr);              \
    m_frameCommands.emplace_back(                                                           \
        [this, StartSlot, NumBuffers, refs]() {                                             \
            ID3D10Buffer* rraw[kMaxUnwrapArray] = { 0 };                                    \
            UINT rcap = NumBuffers <= kMaxUnwrapArray ? NumBuffers : kMaxUnwrapArray;       \
            for (UINT i = 0; i < rcap; ++i)                                                 \
                rraw[i] = UnwrapBuf10(static_cast<ID3D10Buffer*>(refs[i].p));               \
            m_real->STAGE_PREFIX##SetConstantBuffers(StartSlot, NumBuffers, rraw);          \
        });                                                                                 \
}
D3D10_CB_SET(VS, 1)
D3D10_CB_SET(PS, 0)
D3D10_CB_SET(GS, 1)
#undef D3D10_CB_SET

void STDMETHODCALLTYPE Device10Proxy::IASetInputLayout(ID3D10InputLayout* pInputLayout)
{
    m_real->IASetInputLayout(pInputLayout);
    if (!m_presentHookActive) return;
    ComRefHolder10 layoutRef(pInputLayout);
    m_frameCommands.emplace_back(
        [this, layoutRef]() {
            m_real->IASetInputLayout(static_cast<ID3D10InputLayout*>(layoutRef.p));
        });
}

void STDMETHODCALLTYPE Device10Proxy::IASetVertexBuffers(
    UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppVertexBuffers,
    const UINT* pStrides, const UINT* pOffsets)
{
    ID3D10Buffer* raw[kMaxUnwrapArray] = { 0 };
    UINT cap = NumBuffers <= kMaxUnwrapArray ? NumBuffers : kMaxUnwrapArray;
    for (UINT i = 0; i < cap; ++i)
        raw[i] = ppVertexBuffers ? UnwrapBuf10(ppVertexBuffers[i]) : nullptr;
    m_real->IASetVertexBuffers(StartSlot, NumBuffers,
        ppVertexBuffers ? raw : nullptr, pStrides, pOffsets);
    if (!m_presentHookActive) return;
    std::vector<ComRefHolder10> bufRefs;
    bufRefs.reserve(NumBuffers);
    for (UINT i = 0; i < NumBuffers; ++i)
        bufRefs.emplace_back(ppVertexBuffers ? ppVertexBuffers[i] : nullptr);
    std::vector<UINT> strides, offsets;
    if (pStrides) strides.assign(pStrides, pStrides + NumBuffers);
    if (pOffsets) offsets.assign(pOffsets, pOffsets + NumBuffers);
    m_frameCommands.emplace_back(
        [this, StartSlot, NumBuffers, bufRefs, strides, offsets]() {
            ID3D10Buffer* rraw[kMaxUnwrapArray] = { 0 };
            UINT rcap = NumBuffers <= kMaxUnwrapArray ? NumBuffers : kMaxUnwrapArray;
            for (UINT i = 0; i < rcap; ++i)
                rraw[i] = UnwrapBuf10(static_cast<ID3D10Buffer*>(bufRefs[i].p));
            m_real->IASetVertexBuffers(StartSlot, NumBuffers, rraw,
                strides.empty() ? nullptr : strides.data(),
                offsets.empty() ? nullptr : offsets.data());
        });
}

void STDMETHODCALLTYPE Device10Proxy::IASetIndexBuffer(
    ID3D10Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset)
{
    m_real->IASetIndexBuffer(UnwrapBuf10(pIndexBuffer), Format, Offset);
    if (!m_presentHookActive) return;
    ComRefHolder10 bufRef(pIndexBuffer);
    m_frameCommands.emplace_back(
        [this, bufRef, Format, Offset]() {
            m_real->IASetIndexBuffer(UnwrapBuf10(static_cast<ID3D10Buffer*>(bufRef.p)),
                                     Format, Offset);
        });
}

void STDMETHODCALLTYPE Device10Proxy::SOSetTargets(
    UINT NumBuffers, ID3D10Buffer* const* ppSOTargets, const UINT* pOffsets)
{
    ID3D10Buffer* raw[kMaxUnwrapArray] = { 0 };
    UINT cap = NumBuffers <= kMaxUnwrapArray ? NumBuffers : kMaxUnwrapArray;
    for (UINT i = 0; i < cap; ++i)
        raw[i] = ppSOTargets ? UnwrapBuf10(ppSOTargets[i]) : nullptr;
    m_real->SOSetTargets(NumBuffers,
        ppSOTargets ? raw : nullptr, pOffsets);
    if (!m_presentHookActive) return;
    std::vector<ComRefHolder10> bufRefs;
    bufRefs.reserve(NumBuffers);
    for (UINT i = 0; i < NumBuffers; ++i)
        bufRefs.emplace_back(ppSOTargets ? ppSOTargets[i] : nullptr);
    std::vector<UINT> offsets;
    if (pOffsets) offsets.assign(pOffsets, pOffsets + NumBuffers);
    m_frameCommands.emplace_back(
        [this, NumBuffers, bufRefs, offsets]() {
            ID3D10Buffer* rraw[kMaxUnwrapArray] = { 0 };
            UINT rcap = NumBuffers <= kMaxUnwrapArray ? NumBuffers : kMaxUnwrapArray;
            for (UINT i = 0; i < rcap; ++i)
                rraw[i] = UnwrapBuf10(static_cast<ID3D10Buffer*>(bufRefs[i].p));
            m_real->SOSetTargets(NumBuffers, rraw,
                offsets.empty() ? nullptr : offsets.data());
        });
}

// ---------------------------------------------------------------------------
// OMSetRenderTargets — eye-aware. Stage 1 of the port: m_activeEye starts
// Left and never changes (no replay sweep yet for DX10) so the right-eye
// siblings allocated by CreateTexture2D / CreateRenderTargetView are
// allocated but never bound. Stage 4 of the DX10 port will toggle the eye.
// ---------------------------------------------------------------------------
// Split into Do* + recording wrapper so the replay closure can re-run the
// eye selection with whatever m_activeEye is set to at replay time.
static void DoOMSetRenderTargets_internal(
    Device10Proxy* self, ID3D10Device* real, bool pickRight,
    UINT NumViews, ID3D10RenderTargetView* const* ppRenderTargetViews,
    ID3D10DepthStencilView* pDepthStencilView)
{
    ID3D10RenderTargetView* rawRTVs[kMaxUnwrapArray] = { 0 };
    ID3D10RenderTargetView* const* rtvsToUse = ppRenderTargetViews;
    if (NumViews > 0 && ppRenderTargetViews)
    {
        UINT cap = NumViews <= kMaxUnwrapArray ? NumViews : kMaxUnwrapArray;
        for (UINT i = 0; i < cap; ++i)
        {
            RTV10Proxy* p = TryUnwrapRTV_10(ppRenderTargetViews[i]);
            if (!p) { rawRTVs[i] = ppRenderTargetViews[i]; continue; }
            ID3D10RenderTargetView* right = p->GetRealRight();
            rawRTVs[i] = (pickRight && right) ? right : p->GetReal();
        }
        rtvsToUse = rawRTVs;
    }
    ID3D10DepthStencilView* realDSV = pDepthStencilView;
    if (DSV10Proxy* d = TryUnwrapDSV_10(pDepthStencilView))
    {
        ID3D10DepthStencilView* right = d->GetRealRight();
        realDSV = (pickRight && right) ? right : d->GetReal();
    }
    real->OMSetRenderTargets(NumViews, rtvsToUse, realDSV);
    (void)self;  // self unused but kept for future expansion
}

void STDMETHODCALLTYPE Device10Proxy::OMSetRenderTargets(
    UINT NumViews, ID3D10RenderTargetView* const* ppRenderTargetViews,
    ID3D10DepthStencilView* pDepthStencilView)
{
    bool pickRight = (m_activeEye == Eye::Right);
    DoOMSetRenderTargets_internal(this, m_real, pickRight,
                                  NumViews, ppRenderTargetViews, pDepthStencilView);
    if (!m_presentHookActive) return;

    // Capture wrapped pointers so replay re-runs eye-aware unwrap with the
    // active eye set by ReplayFrameCommands.
    std::vector<ComRefHolder10> rtvRefs;
    rtvRefs.reserve(NumViews);
    for (UINT i = 0; i < NumViews; ++i)
        rtvRefs.emplace_back(ppRenderTargetViews ? ppRenderTargetViews[i] : nullptr);
    ComRefHolder10 dsvRef(pDepthStencilView);
    m_frameCommands.emplace_back(
        [this, NumViews, rtvRefs, dsvRef]() {
            ID3D10RenderTargetView* raw[kMaxUnwrapArray] = { 0 };
            UINT cap = NumViews <= kMaxUnwrapArray ? NumViews : kMaxUnwrapArray;
            for (UINT i = 0; i < cap; ++i)
                raw[i] = static_cast<ID3D10RenderTargetView*>(rtvRefs[i].p);
            bool pickRightReplay = (m_activeEye == Eye::Right);
            DoOMSetRenderTargets_internal(this, m_real, pickRightReplay,
                                          NumViews, raw,
                                          static_cast<ID3D10DepthStencilView*>(dsvRef.p));
        });
}

void STDMETHODCALLTYPE Device10Proxy::CopyResource(
    ID3D10Resource* pDstResource, ID3D10Resource* pSrcResource)
{
    bool pickRight = (m_activeEye == Eye::Right);
    m_real->CopyResource(UnwrapResForEye10(pDstResource, pickRight),
                         UnwrapResForEye10(pSrcResource, pickRight));
}

void STDMETHODCALLTYPE Device10Proxy::CopySubresourceRegion(
    ID3D10Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
    UINT DstZ, ID3D10Resource* pSrcResource, UINT SrcSubresource,
    const D3D10_BOX* pSrcBox)
{
    bool pickRight = (m_activeEye == Eye::Right);
    m_real->CopySubresourceRegion(
        UnwrapResForEye10(pDstResource, pickRight), DstSubresource, DstX, DstY, DstZ,
        UnwrapResForEye10(pSrcResource, pickRight), SrcSubresource, pSrcBox);
}

void STDMETHODCALLTYPE Device10Proxy::UpdateSubresource(
    ID3D10Resource* pDstResource, UINT DstSubresource, const D3D10_BOX* pDstBox,
    const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    bool pickRight = (m_activeEye == Eye::Right);
    m_real->UpdateSubresource(UnwrapResForEye10(pDstResource, pickRight),
                              DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void STDMETHODCALLTYPE Device10Proxy::ResolveSubresource(
    ID3D10Resource* pDstResource, UINT DstSubresource,
    ID3D10Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format)
{
    bool pickRight = (m_activeEye == Eye::Right);
    m_real->ResolveSubresource(
        UnwrapResForEye10(pDstResource, pickRight), DstSubresource,
        UnwrapResForEye10(pSrcResource, pickRight), SrcSubresource, Format);
}

static void DoClearRTV_internal(
    ID3D10Device* real, bool pickRight,
    ID3D10RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4])
{
    ID3D10RenderTargetView* r = pRenderTargetView;
    if (auto* rtv = TryUnwrapRTV_10(pRenderTargetView))
    {
        ID3D10RenderTargetView* right = rtv->GetRealRight();
        r = (pickRight && right) ? right : rtv->GetReal();
    }
    real->ClearRenderTargetView(r, ColorRGBA);
}

void STDMETHODCALLTYPE Device10Proxy::ClearRenderTargetView(
    ID3D10RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4])
{
    bool pickRight = (m_activeEye == Eye::Right);
    DoClearRTV_internal(m_real, pickRight, pRenderTargetView, ColorRGBA);
    if (!m_presentHookActive) return;
    ComRefHolder10 rtvRef(pRenderTargetView);
    FLOAT color[4] = { 0, 0, 0, 0 };
    if (ColorRGBA)
    {
        color[0] = ColorRGBA[0]; color[1] = ColorRGBA[1];
        color[2] = ColorRGBA[2]; color[3] = ColorRGBA[3];
    }
    m_frameCommands.emplace_back(
        [this, rtvRef, color]() {
            bool pickRightReplay = (m_activeEye == Eye::Right);
            DoClearRTV_internal(m_real, pickRightReplay,
                static_cast<ID3D10RenderTargetView*>(rtvRef.p), color);
        });
}

static void DoClearDSV_internal(
    ID3D10Device* real, bool pickRight,
    ID3D10DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    ID3D10DepthStencilView* d = pDepthStencilView;
    if (auto* dsv = TryUnwrapDSV_10(pDepthStencilView))
    {
        ID3D10DepthStencilView* right = dsv->GetRealRight();
        d = (pickRight && right) ? right : dsv->GetReal();
    }
    real->ClearDepthStencilView(d, ClearFlags, Depth, Stencil);
}

void STDMETHODCALLTYPE Device10Proxy::ClearDepthStencilView(
    ID3D10DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil)
{
    bool pickRight = (m_activeEye == Eye::Right);
    DoClearDSV_internal(m_real, pickRight, pDepthStencilView, ClearFlags, Depth, Stencil);
    if (!m_presentHookActive) return;
    ComRefHolder10 dsvRef(pDepthStencilView);
    m_frameCommands.emplace_back(
        [this, dsvRef, ClearFlags, Depth, Stencil]() {
            bool pickRightReplay = (m_activeEye == Eye::Right);
            DoClearDSV_internal(m_real, pickRightReplay,
                static_cast<ID3D10DepthStencilView*>(dsvRef.p),
                ClearFlags, Depth, Stencil);
        });
}

// ---------------------------------------------------------------------------
// Recording macros — mirror Context11Proxy's RECORD_* patterns at the D3D10
// API level. Same shape: execute via m_real immediately, then if the Present
// hook is active push a replay closure into m_frameCommands that re-invokes
// at right-eye time.
// ---------------------------------------------------------------------------

// *SetShaderResources (VS / PS / GS)
#define D3D10_SRV_SET(STAGE_PREFIX)                                                         \
void STDMETHODCALLTYPE Device10Proxy::STAGE_PREFIX##SetShaderResources(                     \
    UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews)  \
{                                                                                           \
    m_real->STAGE_PREFIX##SetShaderResources(StartSlot, NumViews, ppShaderResourceViews);   \
    if (!m_presentHookActive) return;                                                       \
    std::vector<ComRefHolder10> refs;                                                       \
    refs.reserve(NumViews);                                                                 \
    for (UINT i = 0; i < NumViews; ++i)                                                     \
        refs.emplace_back(ppShaderResourceViews ? ppShaderResourceViews[i] : nullptr);      \
    m_frameCommands.emplace_back(                                                           \
        [this, StartSlot, NumViews, refs]() {                                               \
            ID3D10ShaderResourceView* raw[kMaxUnwrapArray] = { 0 };                         \
            UINT cap = NumViews <= kMaxUnwrapArray ? NumViews : kMaxUnwrapArray;            \
            for (UINT i = 0; i < cap; ++i)                                                  \
                raw[i] = static_cast<ID3D10ShaderResourceView*>(refs[i].p);                 \
            m_real->STAGE_PREFIX##SetShaderResources(StartSlot, NumViews, raw);             \
        });                                                                                 \
}
D3D10_SRV_SET(VS)
D3D10_SRV_SET(PS)
D3D10_SRV_SET(GS)
#undef D3D10_SRV_SET

// *SetSamplers
#define D3D10_SAMPLER_SET(STAGE_PREFIX)                                                     \
void STDMETHODCALLTYPE Device10Proxy::STAGE_PREFIX##SetSamplers(                            \
    UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers)                \
{                                                                                           \
    m_real->STAGE_PREFIX##SetSamplers(StartSlot, NumSamplers, ppSamplers);                  \
    if (!m_presentHookActive) return;                                                       \
    std::vector<ComRefHolder10> refs;                                                       \
    refs.reserve(NumSamplers);                                                              \
    for (UINT i = 0; i < NumSamplers; ++i)                                                  \
        refs.emplace_back(ppSamplers ? ppSamplers[i] : nullptr);                            \
    m_frameCommands.emplace_back(                                                           \
        [this, StartSlot, NumSamplers, refs]() {                                            \
            ID3D10SamplerState* raw[kMaxUnwrapArray] = { 0 };                               \
            UINT cap = NumSamplers <= kMaxUnwrapArray ? NumSamplers : kMaxUnwrapArray;      \
            for (UINT i = 0; i < cap; ++i)                                                  \
                raw[i] = static_cast<ID3D10SamplerState*>(refs[i].p);                       \
            m_real->STAGE_PREFIX##SetSamplers(StartSlot, NumSamplers, raw);                 \
        });                                                                                 \
}
D3D10_SAMPLER_SET(VS)
D3D10_SAMPLER_SET(PS)
D3D10_SAMPLER_SET(GS)
#undef D3D10_SAMPLER_SET

// *SetShader (single shader, no class instances on D3D10)
#define D3D10_SHADER_SET(STAGE_PREFIX, SHADER_TYPE)                                         \
void STDMETHODCALLTYPE Device10Proxy::STAGE_PREFIX##SetShader(SHADER_TYPE* pShader)         \
{                                                                                           \
    m_real->STAGE_PREFIX##SetShader(pShader);                                               \
    if (!m_presentHookActive) return;                                                       \
    ComRefHolder10 shaderRef(pShader);                                                      \
    m_frameCommands.emplace_back(                                                           \
        [this, shaderRef]() {                                                               \
            m_real->STAGE_PREFIX##SetShader(static_cast<SHADER_TYPE*>(shaderRef.p));        \
        });                                                                                 \
}
D3D10_SHADER_SET(VS, ID3D10VertexShader)
D3D10_SHADER_SET(PS, ID3D10PixelShader)
D3D10_SHADER_SET(GS, ID3D10GeometryShader)
#undef D3D10_SHADER_SET

void STDMETHODCALLTYPE Device10Proxy::IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY Topology)
{
    m_real->IASetPrimitiveTopology(Topology);
    if (!m_presentHookActive) return;
    m_frameCommands.emplace_back([this, Topology]() { m_real->IASetPrimitiveTopology(Topology); });
}

void STDMETHODCALLTYPE Device10Proxy::RSSetState(ID3D10RasterizerState* pRasterizerState)
{
    m_real->RSSetState(pRasterizerState);
    if (!m_presentHookActive) return;
    ComRefHolder10 stateRef(pRasterizerState);
    m_frameCommands.emplace_back(
        [this, stateRef]() { m_real->RSSetState(static_cast<ID3D10RasterizerState*>(stateRef.p)); });
}

void STDMETHODCALLTYPE Device10Proxy::RSSetViewports(UINT NumViewports, const D3D10_VIEWPORT* pViewports)
{
    m_real->RSSetViewports(NumViewports, pViewports);
    if (!m_presentHookActive) return;
    std::vector<D3D10_VIEWPORT> vps;
    if (pViewports) vps.assign(pViewports, pViewports + NumViewports);
    m_frameCommands.emplace_back(
        [this, NumViewports, vps]() {
            m_real->RSSetViewports(NumViewports, vps.empty() ? nullptr : vps.data());
        });
}

void STDMETHODCALLTYPE Device10Proxy::RSSetScissorRects(UINT NumRects, const D3D10_RECT* pRects)
{
    m_real->RSSetScissorRects(NumRects, pRects);
    if (!m_presentHookActive) return;
    std::vector<D3D10_RECT> rects;
    if (pRects) rects.assign(pRects, pRects + NumRects);
    m_frameCommands.emplace_back(
        [this, NumRects, rects]() {
            m_real->RSSetScissorRects(NumRects, rects.empty() ? nullptr : rects.data());
        });
}

void STDMETHODCALLTYPE Device10Proxy::OMSetBlendState(
    ID3D10BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask)
{
    m_real->OMSetBlendState(pBlendState, BlendFactor, SampleMask);
    if (!m_presentHookActive) return;
    ComRefHolder10 stateRef(pBlendState);
    FLOAT factor[4] = { 0, 0, 0, 0 };
    bool hasFactor = (BlendFactor != nullptr);
    if (hasFactor) { factor[0] = BlendFactor[0]; factor[1] = BlendFactor[1];
                     factor[2] = BlendFactor[2]; factor[3] = BlendFactor[3]; }
    m_frameCommands.emplace_back(
        [this, stateRef, factor, hasFactor, SampleMask]() {
            m_real->OMSetBlendState(static_cast<ID3D10BlendState*>(stateRef.p),
                                     hasFactor ? factor : nullptr, SampleMask);
        });
}

void STDMETHODCALLTYPE Device10Proxy::OMSetDepthStencilState(
    ID3D10DepthStencilState* pDepthStencilState, UINT StencilRef)
{
    m_real->OMSetDepthStencilState(pDepthStencilState, StencilRef);
    if (!m_presentHookActive) return;
    ComRefHolder10 stateRef(pDepthStencilState);
    m_frameCommands.emplace_back(
        [this, stateRef, StencilRef]() {
            m_real->OMSetDepthStencilState(static_cast<ID3D10DepthStencilState*>(stateRef.p), StencilRef);
        });
}

// Draws — pure POD captures.
void STDMETHODCALLTYPE Device10Proxy::Draw(UINT VertexCount, UINT StartVertexLocation)
{
    m_real->Draw(VertexCount, StartVertexLocation);
    if (!m_presentHookActive) return;
    m_frameCommands.emplace_back(
        [this, VertexCount, StartVertexLocation]() {
            m_real->Draw(VertexCount, StartVertexLocation);
        });
}

void STDMETHODCALLTYPE Device10Proxy::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
{
    m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
    if (!m_presentHookActive) return;
    m_frameCommands.emplace_back(
        [this, IndexCount, StartIndexLocation, BaseVertexLocation]() {
            m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
        });
}

void STDMETHODCALLTYPE Device10Proxy::DrawInstanced(
    UINT VertexCountPerInstance, UINT InstanceCount,
    UINT StartVertexLocation, UINT StartInstanceLocation)
{
    m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    if (!m_presentHookActive) return;
    m_frameCommands.emplace_back(
        [this, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation]() {
            m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
        });
}

void STDMETHODCALLTYPE Device10Proxy::DrawIndexedInstanced(
    UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
    INT BaseVertexLocation, UINT StartInstanceLocation)
{
    m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation,
                                  BaseVertexLocation, StartInstanceLocation);
    if (!m_presentHookActive) return;
    m_frameCommands.emplace_back(
        [this, IndexCountPerInstance, InstanceCount, StartIndexLocation,
         BaseVertexLocation, StartInstanceLocation]() {
            m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation,
                                          BaseVertexLocation, StartInstanceLocation);
        });
}

void STDMETHODCALLTYPE Device10Proxy::DrawAuto()
{
    m_real->DrawAuto();
    if (!m_presentHookActive) return;
    m_frameCommands.emplace_back([this]() { m_real->DrawAuto(); });
}

} // namespace wiz3d
