#include "SwapChainProxy.h"
#include "Device10Proxy.h"
#include "eye_state.h"
#include "log.h"

#include <d3dcommon.h>
#include <string.h>

#pragma comment(lib, "dxguid.lib")

extern "C" int NvDM_OutputIsTopBottom();
extern "C" int NvDM_SwapEyes();

// ---------------------------------------------------------------------------
// d3dcompiler resolver + composite shader source (same shaders as d3d11;
// vs_4_0 / ps_4_0 are valid for both d3d10 and d3d11)
// ---------------------------------------------------------------------------
namespace {

typedef HRESULT (WINAPI *PFN_D3DCompile)(
    LPCVOID, SIZE_T, LPCSTR,
    const D3D_SHADER_MACRO*, ID3DInclude*,
    LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);

static PFN_D3DCompile g_pfnD3DCompile = nullptr;
static volatile LONG g_d3dCompileResolved = 0;

bool EnsureD3DCompile()
{
    if (g_pfnD3DCompile) return true;
    if (InterlockedCompareExchange(&g_d3dCompileResolved, 1, 0) != 0)
        return g_pfnD3DCompile != nullptr;
    HMODULE h = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!h) h = LoadLibraryW(L"d3dcompiler_46.dll");
    if (!h) h = LoadLibraryW(L"d3dcompiler_43.dll");
    if (!h) return false;
    g_pfnD3DCompile = (PFN_D3DCompile)GetProcAddress(h, "D3DCompile");
    return g_pfnD3DCompile != nullptr;
}

const char kCompositeVS[] =
    "struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VS_OUT main(uint id : SV_VertexID) {\n"
    "  VS_OUT o;\n"
    "  o.uv = float2((id == 1) ? 2.0 : 0.0, (id == 2) ? 2.0 : 0.0);\n"
    "  o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
    "  return o;\n"
    "}\n";

const char kCompositePS_SBS[] =
    "Texture2D leftTex  : register(t0);\n"
    "Texture2D rightTex : register(t1);\n"
    "SamplerState sLinear : register(s0);\n"
    "struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VS_OUT i) : SV_Target {\n"
    "  if (i.uv.x < 0.5)\n"
    "    return leftTex.Sample(sLinear, float2(i.uv.x * 2.0, i.uv.y));\n"
    "  else\n"
    "    return rightTex.Sample(sLinear, float2((i.uv.x - 0.5) * 2.0, i.uv.y));\n"
    "}\n";

const char kCompositePS_TB[] =
    "Texture2D leftTex  : register(t0);\n"
    "Texture2D rightTex : register(t1);\n"
    "SamplerState sLinear : register(s0);\n"
    "struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "float4 main(VS_OUT i) : SV_Target {\n"
    "  if (i.uv.y < 0.5)\n"
    "    return leftTex.Sample(sLinear, float2(i.uv.x, i.uv.y * 2.0));\n"
    "  else\n"
    "    return rightTex.Sample(sLinear, float2(i.uv.x, (i.uv.y - 0.5) * 2.0));\n"
    "}\n";

bool CompileShader(const char* src, size_t len, const char* tag, const char* target, ID3DBlob** out)
{
    if (!g_pfnD3DCompile) return false;
    ID3DBlob* err = nullptr;
    HRESULT hr = g_pfnD3DCompile(src, len, tag, nullptr, nullptr,
                                  "main", target, 0, 0, out, &err);
    if (err) { err->Release(); err = nullptr; }
    return SUCCEEDED(hr) && *out != nullptr;
}

} // anonymous namespace

namespace NvDirectMode
{

// Primary-swap-chain registry for the eye-change callback dispatcher.
namespace
{
    SwapChainProxy*  g_primarySwapChain = nullptr;
    CRITICAL_SECTION g_primaryLock;
    bool             g_primaryLockInit = false;

    void EnsurePrimaryLock()
    {
        if (g_primaryLockInit) return;
        InitializeCriticalSection(&g_primaryLock);
        g_primaryLockInit = true;
    }

    void OnEyeChange(int oldEye, int /*newEye*/)
    {
        EnsurePrimaryLock();
        EnterCriticalSection(&g_primaryLock);
        if (g_primarySwapChain)
            g_primarySwapChain->CaptureEye(oldEye);
        LeaveCriticalSection(&g_primaryLock);
    }
}

SwapChainProxy::SwapChainProxy(IDXGISwapChain* real, Device10Proxy* parent)
    : m_real(real)
    , m_parent(parent)
    , m_refs(1)
    , m_shadowBB(nullptr)
    , m_logicalW(0)
    , m_logicalH(0)
    , m_shadowFormat(DXGI_FORMAT_UNKNOWN)
    , m_leftEyeFrame(nullptr)
    , m_rightEyeFrame(nullptr)
    , m_lastSeenEye(NvDirectMode::kEyeMono)
    , m_compositeVS(nullptr)
    , m_compositePS_SBS(nullptr)
    , m_compositePS_TB(nullptr)
    , m_compositeSampler(nullptr)
    , m_compositeRS(nullptr)
    , m_compositeBlend(nullptr)
    , m_compositeDSS(nullptr)
    , m_leftEyeSRV(nullptr)
    , m_rightEyeSRV(nullptr)
    , m_realBBRTV(nullptr)
{
    EnsurePrimaryLock();
    EnterCriticalSection(&g_primaryLock);
    g_primarySwapChain = this;
    LeaveCriticalSection(&g_primaryLock);
    NvDirectMode::RegisterEyeChangeHandler(&OnEyeChange);
}

SwapChainProxy::~SwapChainProxy()
{
    EnsurePrimaryLock();
    EnterCriticalSection(&g_primaryLock);
    if (g_primarySwapChain == this) g_primarySwapChain = nullptr;
    LeaveCriticalSection(&g_primaryLock);

    ReleaseCompositePipeline();
    ReleaseEyeFrames();
    ReleaseShadowBB();
    if (m_real) { m_real->Release(); m_real = nullptr; }
}

void SwapChainProxy::ReleaseShadowBB()
{
    if (m_shadowBB) { m_shadowBB->Release(); m_shadowBB = nullptr; }
    m_logicalW = 0;
    m_logicalH = 0;
    m_shadowFormat = DXGI_FORMAT_UNKNOWN;
}

void SwapChainProxy::ReleaseEyeFrames()
{
    if (m_leftEyeSRV)    { m_leftEyeSRV->Release();    m_leftEyeSRV = nullptr; }
    if (m_rightEyeSRV)   { m_rightEyeSRV->Release();   m_rightEyeSRV = nullptr; }
    if (m_leftEyeFrame)  { m_leftEyeFrame->Release();  m_leftEyeFrame = nullptr; }
    if (m_rightEyeFrame) { m_rightEyeFrame->Release(); m_rightEyeFrame = nullptr; }
    m_lastSeenEye = NvDirectMode::kEyeMono;
}

void SwapChainProxy::ReleaseCompositePipeline()
{
    if (m_realBBRTV)        { m_realBBRTV->Release();        m_realBBRTV = nullptr; }
    if (m_leftEyeSRV)       { m_leftEyeSRV->Release();       m_leftEyeSRV = nullptr; }
    if (m_rightEyeSRV)      { m_rightEyeSRV->Release();      m_rightEyeSRV = nullptr; }
    if (m_compositeDSS)     { m_compositeDSS->Release();     m_compositeDSS = nullptr; }
    if (m_compositeBlend)   { m_compositeBlend->Release();   m_compositeBlend = nullptr; }
    if (m_compositeRS)      { m_compositeRS->Release();      m_compositeRS = nullptr; }
    if (m_compositeSampler) { m_compositeSampler->Release(); m_compositeSampler = nullptr; }
    if (m_compositePS_TB)   { m_compositePS_TB->Release();   m_compositePS_TB = nullptr; }
    if (m_compositePS_SBS)  { m_compositePS_SBS->Release();  m_compositePS_SBS = nullptr; }
    if (m_compositeVS)      { m_compositeVS->Release();      m_compositeVS = nullptr; }
}

void SwapChainProxy::EnsureShadowBB()
{
    if (m_shadowBB || !m_real || !m_parent) return;

    DXGI_SWAP_CHAIN_DESC desc = {};
    if (FAILED(m_real->GetDesc(&desc))) return;
    m_logicalW = desc.BufferDesc.Width;
    m_logicalH = desc.BufferDesc.Height;
    m_shadowFormat = desc.BufferDesc.Format;
    if (m_logicalW == 0 || m_logicalH == 0) return;

    m_parent->SetLogicalBackBufferSize(m_logicalW, m_logicalH);

    ID3D10Device* dev = m_parent->GetReal();
    if (!dev) return;

    D3D10_TEXTURE2D_DESC td = {};
    td.Width            = m_logicalW;
    td.Height           = m_logicalH;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = m_shadowFormat;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D10_USAGE_DEFAULT;
    td.BindFlags        = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;

    HRESULT hr = dev->CreateTexture2D(&td, nullptr, &m_shadowBB);
    if (FAILED(hr) || !m_shadowBB)
    {
        LOG_VERBOSE("  d3d10 EnsureShadowBB: CreateTexture2D(%ux%u) FAILED hr=0x%08lX\n",
                    m_logicalW, m_logicalH, hr);
        m_shadowBB = nullptr;
        return;
    }
    LOG_VERBOSE("  d3d10 EnsureShadowBB: shadow=%p (%ux%u 1x logical, fmt=%d)\n",
                m_shadowBB, m_logicalW, m_logicalH, (int)m_shadowFormat);
}

void SwapChainProxy::CaptureEye(int eyeBeingLeft)
{
    if (!m_shadowBB || !m_parent) return;

    ID3D10Texture2D** slot = nullptr;
    if      (eyeBeingLeft == NvDirectMode::kEyeLeft)  slot = &m_leftEyeFrame;
    else if (eyeBeingLeft == NvDirectMode::kEyeRight) slot = &m_rightEyeFrame;
    else return;

    if (!*slot)
    {
        ID3D10Device* dev = m_parent->GetReal();
        if (!dev) return;
        D3D10_TEXTURE2D_DESC td = {};
        td.Width            = m_logicalW;
        td.Height           = m_logicalH;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = m_shadowFormat;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D10_USAGE_DEFAULT;
        td.BindFlags        = D3D10_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, slot)) || !*slot) return;
        LOG_VERBOSE("  d3d10 CaptureEye(%d): allocated eye texture=%p\n", eyeBeingLeft, *slot);
    }

    ID3D10Device* dev = m_parent->GetReal();
    if (!dev) return;
    dev->CopyResource(*slot, m_shadowBB);
    NVDM_TRACE_FIRST_N(8, "  d3d10 CaptureEye(eye=%d): copied shadow=%p -> eyeFrame=%p\n",
                       eyeBeingLeft, m_shadowBB, *slot);
}

bool SwapChainProxy::EnsureCompositeShaders()
{
    if (!m_parent) return false;
    if (m_compositeVS && m_compositePS_SBS && m_compositePS_TB &&
        m_compositeSampler && m_compositeRS && m_compositeBlend && m_compositeDSS)
        return true;
    if (!EnsureD3DCompile()) return false;

    ID3D10Device* dev = m_parent->GetReal();
    if (!dev) return false;

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psSbsBlob = nullptr;
    ID3DBlob* psTbBlob = nullptr;
    bool ok = true;
    ok = ok && CompileShader(kCompositeVS,     sizeof(kCompositeVS) - 1,     "vs",  "vs_4_0", &vsBlob);
    ok = ok && CompileShader(kCompositePS_SBS, sizeof(kCompositePS_SBS) - 1, "sbs", "ps_4_0", &psSbsBlob);
    ok = ok && CompileShader(kCompositePS_TB,  sizeof(kCompositePS_TB) - 1,  "tb",  "ps_4_0", &psTbBlob);
    if (!ok)
    {
        if (vsBlob)    vsBlob->Release();
        if (psSbsBlob) psSbsBlob->Release();
        if (psTbBlob)  psTbBlob->Release();
        return false;
    }

    HRESULT hr = S_OK;
    if (!m_compositeVS)
        hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_compositeVS);
    if (SUCCEEDED(hr) && !m_compositePS_SBS)
        hr = dev->CreatePixelShader(psSbsBlob->GetBufferPointer(), psSbsBlob->GetBufferSize(), &m_compositePS_SBS);
    if (SUCCEEDED(hr) && !m_compositePS_TB)
        hr = dev->CreatePixelShader(psTbBlob->GetBufferPointer(), psTbBlob->GetBufferSize(), &m_compositePS_TB);
    vsBlob->Release(); psSbsBlob->Release(); psTbBlob->Release();
    if (FAILED(hr)) { ReleaseCompositePipeline(); return false; }

    if (!m_compositeSampler)
    {
        D3D10_SAMPLER_DESC sd = {};
        sd.Filter   = D3D10_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
        sd.MinLOD = 0; sd.MaxLOD = D3D10_FLOAT32_MAX;
        dev->CreateSamplerState(&sd, &m_compositeSampler);
    }
    if (!m_compositeRS)
    {
        D3D10_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D10_FILL_SOLID;
        rd.CullMode = D3D10_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        dev->CreateRasterizerState(&rd, &m_compositeRS);
    }
    if (!m_compositeBlend)
    {
        D3D10_BLEND_DESC bd = {};
        bd.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bd, &m_compositeBlend);
    }
    if (!m_compositeDSS)
    {
        D3D10_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable   = FALSE;
        dsd.StencilEnable = FALSE;
        dev->CreateDepthStencilState(&dsd, &m_compositeDSS);
    }
    bool full = m_compositeVS && m_compositePS_SBS && m_compositePS_TB &&
                m_compositeSampler && m_compositeRS && m_compositeBlend && m_compositeDSS;
    LOG_VERBOSE("  d3d10 EnsureCompositeShaders: %s\n", full ? "OK" : "PARTIAL");
    return full;
}

bool SwapChainProxy::RunCompositePass()
{
    if (!m_leftEyeFrame || !m_rightEyeFrame) return false;
    if (!EnsureCompositeShaders()) return false;

    ID3D10Device* dev = m_parent ? m_parent->GetReal() : nullptr;
    if (!dev) return false;

    if (!m_leftEyeSRV)
    {
        D3D10_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format              = m_shadowFormat;
        sd.ViewDimension       = D3D10_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(m_leftEyeFrame, &sd, &m_leftEyeSRV))) return false;
    }
    if (!m_rightEyeSRV)
    {
        D3D10_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format              = m_shadowFormat;
        sd.ViewDimension       = D3D10_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(m_rightEyeFrame, &sd, &m_rightEyeSRV))) return false;
    }
    if (!m_realBBRTV)
    {
        ID3D10Texture2D* realBB = nullptr;
        if (FAILED(m_real->GetBuffer(0, __uuidof(ID3D10Texture2D),
                                      reinterpret_cast<void**>(&realBB))) || !realBB)
            return false;
        HRESULT hr = dev->CreateRenderTargetView(realBB, nullptr, &m_realBBRTV);
        realBB->Release();
        if (FAILED(hr) || !m_realBBRTV) return false;
    }

    bool swap = (NvDM_SwapEyes() != 0);
    ID3D10ShaderResourceView* leftSRV  = swap ? m_rightEyeSRV : m_leftEyeSRV;
    ID3D10ShaderResourceView* rightSRV = swap ? m_leftEyeSRV  : m_rightEyeSRV;
    ID3D10ShaderResourceView* srvs[2] = { leftSRV, rightSRV };

    bool topBottom = NvDM_OutputIsTopBottom() != 0;
    ID3D10PixelShader* ps = topBottom ? m_compositePS_TB : m_compositePS_SBS;

    D3D10_VIEWPORT vp = {};
    vp.Width  = m_logicalW;
    vp.Height = m_logicalH;
    vp.MinDepth = 0; vp.MaxDepth = 1;

    dev->OMSetRenderTargets(1, &m_realBBRTV, nullptr);
    dev->RSSetViewports(1, &vp);
    dev->RSSetState(m_compositeRS);
    float blendFactor[4] = { 0, 0, 0, 0 };
    dev->OMSetBlendState(m_compositeBlend, blendFactor, 0xFFFFFFFF);
    dev->OMSetDepthStencilState(m_compositeDSS, 0);
    dev->IASetInputLayout(nullptr);
    dev->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D10Buffer* nullVB = nullptr; UINT zero = 0;
    dev->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
    dev->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dev->VSSetShader(m_compositeVS);
    dev->GSSetShader(nullptr);
    dev->PSSetShader(ps);
    dev->PSSetShaderResources(0, 2, srvs);
    dev->PSSetSamplers(0, 1, &m_compositeSampler);
    dev->Draw(3, 0);

    ID3D10ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    dev->PSSetShaderResources(0, 2, nullSRV);

    NVDM_TRACE_FIRST_N(2, "  d3d10 RunCompositePass: %s eyes (swap=%d) -> realBB rtv=%p\n",
                       topBottom ? "T-B" : "SBS", (int)swap, m_realBBRTV);
    return true;
}

void SwapChainProxy::CaptureAndPresentBlit()
{
    if (!m_shadowBB || !m_real || !m_parent) return;

    int currentEye = NvDirectMode::GetActiveEye();
    if (currentEye == NvDirectMode::kEyeLeft || currentEye == NvDirectMode::kEyeRight)
    {
        CaptureEye(currentEye);
        m_lastSeenEye = currentEye;
    }

    if (RunCompositePass()) return;

    // Fallback: single-eye copy
    ID3D10Texture2D* realBB = nullptr;
    if (FAILED(m_real->GetBuffer(0, __uuidof(ID3D10Texture2D),
                                  reinterpret_cast<void**>(&realBB))) || !realBB)
        return;

    ID3D10Device* dev = m_parent->GetReal();
    bool swap = NvDM_SwapEyes() != 0;
    ID3D10Texture2D* leftSrc  = swap ? m_rightEyeFrame : m_leftEyeFrame;
    ID3D10Texture2D* rightSrc = swap ? m_leftEyeFrame  : m_rightEyeFrame;
    ID3D10Texture2D* displaySrc = leftSrc ? leftSrc : (rightSrc ? rightSrc : m_shadowBB);
    if (dev) dev->CopyResource(realBB, displaySrc);
    NVDM_TRACE_FIRST_N(4, "  d3d10 CaptureAndPresentBlit (fallback): currentEye=%d displaySrc=%s\n",
                       currentEye,
                       (displaySrc == m_leftEyeFrame  ? "leftEye"  :
                        displaySrc == m_rightEyeFrame ? "rightEye" : "shadow"));
    realBB->Release();
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::Present(UINT SyncInterval, UINT Flags)
{
    NVDM_TRACE_FIRST_N(4, "  d3d10 SwapChainProxy::Present(SyncInterval=%u, Flags=0x%X)\n", SyncInterval, Flags);
    CaptureAndPresentBlit();
    return m_real->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDXGIObject ||
        riid == IID_IDXGIDeviceSubObject || riid == IID_IDXGISwapChain)
    {
        *ppvObj = static_cast<IDXGISwapChain*>(this);
        AddRef();
        return S_OK;
    }
    NVDM_TRACE_FIRST_N(8, "  d3d10/SwapChainProxy::QI(unknown/higher IID) -> E_NOINTERFACE\n");
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    if (Buffer == 0)
    {
        EnsureShadowBB();
        if (m_shadowBB && ppSurface)
        {
            HRESULT hr = m_shadowBB->QueryInterface(riid, ppSurface);
            if (SUCCEEDED(hr) && *ppSurface && m_parent)
            {
                m_parent->RegisterBackBufferTexture(*ppSurface);
                LOG_VERBOSE("  d3d10/SwapChainProxy::GetBuffer(0): handed shadow %p (as %p) on parent=%p\n",
                            m_shadowBB, *ppSurface, m_parent);
            }
            return hr;
        }
    }
    HRESULT hr = m_real->GetBuffer(Buffer, riid, ppSurface);
    NVDM_TRACE_FIRST_N(4, "  d3d10/SwapChainProxy::GetBuffer(idx=%u) hr=0x%08lX (passthrough)\n", Buffer, hr);
    return hr;
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::ResizeBuffers(
    UINT BufferCount, UINT Width, UINT Height,
    DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    LOG_VERBOSE("  d3d10/SwapChainProxy::ResizeBuffers(%ux%u, fmt=%d, flags=0x%X)\n",
                Width, Height, (int)NewFormat, SwapChainFlags);
    if (m_realBBRTV) { m_realBBRTV->Release(); m_realBBRTV = nullptr; }
    ReleaseEyeFrames();
    ReleaseShadowBB();
    return m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

} // namespace NvDirectMode
