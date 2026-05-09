#include "SwapChainProxy.h"
#include "Device11Proxy.h"
#include "eye_state.h"
#include "log.h"

#include <d3dcommon.h>
#include <string.h>

#pragma comment(lib, "dxguid.lib")  // for IID_IDXGISwapChain et al

// Output-mode + swap-eyes flags (from dllmain.cpp via the C-linkage bridge).
extern "C" int NvDM_OutputIsTopBottom();
extern "C" int NvDM_SwapEyes();

// ---------------------------------------------------------------------------
// Stage 4b composite pipeline: D3DCompile resolver + HLSL source
// ---------------------------------------------------------------------------
namespace {

typedef HRESULT (WINAPI *PFN_D3DCompile)(
    LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
    LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2,
    ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs);

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
    if (!h)
    {
        LOG_VERBOSE("  EnsureD3DCompile: no d3dcompiler_*.dll found — composite path disabled\n");
        return false;
    }
    g_pfnD3DCompile = (PFN_D3DCompile)GetProcAddress(h, "D3DCompile");
    if (!g_pfnD3DCompile)
        LOG_VERBOSE("  EnsureD3DCompile: GetProcAddress(D3DCompile) failed in %p\n", h);
    return g_pfnD3DCompile != nullptr;
}

// Fullscreen-triangle VS — no vertex buffer needed, uses SV_VertexID. id 0
// gives uv=(0,0), id 1 gives uv=(2,0), id 2 gives uv=(0,2). The triangle
// covers [-1,1]x[-1,1] in NDC; the half outside the screen is clipped.
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

bool CompileShader(const char* src, size_t len, const char* entry, const char* target,
                   ID3DBlob** out)
{
    if (!g_pfnD3DCompile) return false;
    ID3DBlob* err = nullptr;
    HRESULT hr = g_pfnD3DCompile(src, len, entry, nullptr, nullptr,
                                  "main", target, 0, 0, out, &err);
    if (err) { err->Release(); err = nullptr; }
    return SUCCEEDED(hr) && *out != nullptr;
}

} // anonymous namespace

namespace NvDirectMode
{

// ---------------------------------------------------------------------------
// Stage 4 primary-swap-chain registry: NvApiProxy's eye-change callback
// runs as plain C-linkage with no `this` context, so we keep a global
// pointer to the most recently created SwapChainProxy. The callback
// dispatches into that primary's CaptureEye(). For multi-swap-chain
// games (rare), only the primary captures; secondary swap chains operate
// without per-eye capture (display whichever eye was last rendered, same
// as stage 3 v2 behaviour).
// ---------------------------------------------------------------------------
namespace
{
    SwapChainProxy* g_primarySwapChain = nullptr;
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

SwapChainProxy::SwapChainProxy(IDXGISwapChain* real, Device11Proxy* parent)
    : m_real(real)
    , m_real1(nullptr)
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
    if (m_real)
        m_real->QueryInterface(IID_IDXGISwapChain1, reinterpret_cast<void**>(&m_real1));

    // Register as the primary swap chain (replaces any previous).
    EnsurePrimaryLock();
    EnterCriticalSection(&g_primaryLock);
    g_primarySwapChain = this;
    LeaveCriticalSection(&g_primaryLock);

    // Lazily register the eye-change handler with NvApiProxy. Idempotent
    // — the eye_state module remembers we've registered.
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

void SwapChainProxy::ReleaseShadowBB()
{
    if (m_shadowBB) { m_shadowBB->Release(); m_shadowBB = nullptr; }
    m_logicalW = 0;
    m_logicalH = 0;
    m_shadowFormat = DXGI_FORMAT_UNKNOWN;
}

void SwapChainProxy::ReleaseEyeFrames()
{
    // SRVs hold a ref on the underlying texture — release them first so
    // the texture refcount decrements correctly when we Release the slot.
    if (m_leftEyeSRV)    { m_leftEyeSRV->Release();    m_leftEyeSRV = nullptr; }
    if (m_rightEyeSRV)   { m_rightEyeSRV->Release();   m_rightEyeSRV = nullptr; }
    if (m_leftEyeFrame)  { m_leftEyeFrame->Release();  m_leftEyeFrame = nullptr; }
    if (m_rightEyeFrame) { m_rightEyeFrame->Release(); m_rightEyeFrame = nullptr; }
    m_lastSeenEye = NvDirectMode::kEyeMono;
}

void SwapChainProxy::EnsureEyeFrames()
{
    // Stage 4 v1.1: this function is now a no-op intentionally — eye
    // frames are allocated lazily *per eye* in CaptureEye(). Eager-
    // allocating both meant null != never-captured (it meant null !=
    // never-allocated), so a SwapEyes=1 display would end up copying
    // an empty-but-allocated right-eye texture to the real BB and
    // showing a black screen until the right-eye actually got captured.
}

void SwapChainProxy::CaptureEye(int eyeBeingLeft)
{
    if (!m_shadowBB || !m_parent) return;

    ID3D11Texture2D** slot = nullptr;
    if      (eyeBeingLeft == NvDirectMode::kEyeLeft)  slot = &m_leftEyeFrame;
    else if (eyeBeingLeft == NvDirectMode::kEyeRight) slot = &m_rightEyeFrame;
    else return; // MONO transitions don't need capturing — first real eye render starts fresh

    // Lazy per-eye allocation: only create this eye's texture on its
    // first capture. Means a non-null slot pointer means "this eye has
    // actual game-rendered content in it", which the display logic
    // relies on.
    if (!*slot)
    {
        ID3D11Device* dev = m_parent->GetReal();
        if (!dev) return;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width            = m_logicalW;
        td.Height           = m_logicalH;
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = m_shadowFormat;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = dev->CreateTexture2D(&td, nullptr, slot);
        if (FAILED(hr) || !*slot)
        {
            LOG_VERBOSE("  CaptureEye(%d): per-eye CreateTexture2D FAILED hr=0x%08lX\n",
                        eyeBeingLeft, hr);
            return;
        }
        LOG_VERBOSE("  CaptureEye(%d): allocated eye texture=%p (%ux%u fmt=%d)\n",
                    eyeBeingLeft, *slot, m_logicalW, m_logicalH, (int)m_shadowFormat);
    }

    ID3D11DeviceContext* ctx = nullptr;
    if (m_parent->GetReal()) m_parent->GetReal()->GetImmediateContext(&ctx);
    if (ctx)
    {
        ctx->CopyResource(*slot, m_shadowBB);
        ctx->Release();
    }
    NVDM_TRACE_FIRST_N(8, "  CaptureEye(eye=%d): copied shadow=%p -> eyeFrame=%p\n",
                       eyeBeingLeft, m_shadowBB, *slot);
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
    CaptureAndPresentBlit();
    return m_real->Present(SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE SwapChainProxy::Present1(UINT SyncInterval, UINT Flags,
                                                   const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    NVDM_TRACE_FIRST_N(4, "  SwapChainProxy::Present1(SyncInterval=%u, Flags=0x%X)\n", SyncInterval, Flags);
    if (!m_real1) return E_NOINTERFACE;
    CaptureAndPresentBlit();
    return m_real1->Present1(SyncInterval, Flags, pPresentParameters);
}

bool SwapChainProxy::EnsureCompositeShaders()
{
    if (!m_parent) return false;
    if (m_compositeVS && m_compositePS_SBS && m_compositePS_TB &&
        m_compositeSampler && m_compositeRS && m_compositeBlend && m_compositeDSS)
        return true;

    if (!EnsureD3DCompile()) return false;

    ID3D11Device* dev = m_parent->GetReal();
    if (!dev) return false;

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psSbsBlob = nullptr;
    ID3DBlob* psTbBlob = nullptr;
    bool ok = true;
    ok = ok && CompileShader(kCompositeVS,      sizeof(kCompositeVS) - 1,      "vs", "vs_4_0", &vsBlob);
    ok = ok && CompileShader(kCompositePS_SBS,  sizeof(kCompositePS_SBS) - 1,  "ps", "ps_4_0", &psSbsBlob);
    ok = ok && CompileShader(kCompositePS_TB,   sizeof(kCompositePS_TB) - 1,   "ps", "ps_4_0", &psTbBlob);
    if (!ok)
    {
        LOG_VERBOSE("  EnsureCompositeShaders: D3DCompile failed (vs=%p sbs=%p tb=%p)\n",
                    vsBlob, psSbsBlob, psTbBlob);
        if (vsBlob)    vsBlob->Release();
        if (psSbsBlob) psSbsBlob->Release();
        if (psTbBlob)  psTbBlob->Release();
        return false;
    }

    HRESULT hr = S_OK;
    if (!m_compositeVS)
        hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                      nullptr, &m_compositeVS);
    if (SUCCEEDED(hr) && !m_compositePS_SBS)
        hr = dev->CreatePixelShader(psSbsBlob->GetBufferPointer(), psSbsBlob->GetBufferSize(),
                                     nullptr, &m_compositePS_SBS);
    if (SUCCEEDED(hr) && !m_compositePS_TB)
        hr = dev->CreatePixelShader(psTbBlob->GetBufferPointer(), psTbBlob->GetBufferSize(),
                                     nullptr, &m_compositePS_TB);
    vsBlob->Release(); psSbsBlob->Release(); psTbBlob->Release();
    if (FAILED(hr)) { ReleaseCompositePipeline(); return false; }

    if (!m_compositeSampler)
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MinLOD   = 0;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&sd, &m_compositeSampler);
    }
    if (!m_compositeRS)
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode        = D3D11_FILL_SOLID;
        rd.CullMode        = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        dev->CreateRasterizerState(&rd, &m_compositeRS);
    }
    if (!m_compositeBlend)
    {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bd, &m_compositeBlend);
    }
    if (!m_compositeDSS)
    {
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable   = FALSE;
        dsd.StencilEnable = FALSE;
        dev->CreateDepthStencilState(&dsd, &m_compositeDSS);
    }

    bool full = m_compositeVS && m_compositePS_SBS && m_compositePS_TB &&
                m_compositeSampler && m_compositeRS && m_compositeBlend && m_compositeDSS;
    LOG_VERBOSE("  EnsureCompositeShaders: %s\n", full ? "OK" : "PARTIAL");
    return full;
}

bool SwapChainProxy::RunCompositePass()
{
    // Need both eyes captured + shaders + RTV available.
    if (!m_leftEyeFrame || !m_rightEyeFrame) return false;
    if (!EnsureCompositeShaders()) return false;

    ID3D11Device* dev = m_parent ? m_parent->GetReal() : nullptr;
    if (!dev) return false;

    // Lazy SRV creation, one per eye texture (texture pointer stable
    // across captures — CopyResource doesn't change the resource).
    if (!m_leftEyeSRV)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format              = m_shadowFormat;
        sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(m_leftEyeFrame, &sd, &m_leftEyeSRV)))
            return false;
    }
    if (!m_rightEyeSRV)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format              = m_shadowFormat;
        sd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(dev->CreateShaderResourceView(m_rightEyeFrame, &sd, &m_rightEyeSRV)))
            return false;
    }

    // Lazy RTV for the real BB.
    if (!m_realBBRTV)
    {
        ID3D11Texture2D* realBB = nullptr;
        if (FAILED(m_real->GetBuffer(0, IID_ID3D11Texture2D,
                                      reinterpret_cast<void**>(&realBB))) || !realBB)
            return false;
        HRESULT hr = dev->CreateRenderTargetView(realBB, nullptr, &m_realBBRTV);
        realBB->Release();
        if (FAILED(hr) || !m_realBBRTV) return false;
    }

    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    if (!ctx) return false;

    // SwapEyes flips which captured eye lands in which display half.
    bool swap = (NvDM_SwapEyes() != 0);
    ID3D11ShaderResourceView* leftSRV  = swap ? m_rightEyeSRV : m_leftEyeSRV;
    ID3D11ShaderResourceView* rightSRV = swap ? m_leftEyeSRV  : m_rightEyeSRV;
    ID3D11ShaderResourceView* srvs[2] = { leftSRV, rightSRV };

    bool topBottom = NvDM_OutputIsTopBottom() != 0;
    ID3D11PixelShader* ps = topBottom ? m_compositePS_TB : m_compositePS_SBS;

    // Composite pass: write the SBS / T-B framebuffer into the real BB.
    // We do NOT save/restore the game's pipeline state — game rebinds
    // everything on its next frame's first render. (Trade-off documented
    // in the class comment; a save/restore wrapper can be added if a
    // game proves to depend on state persisting across Present.)
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width  = (FLOAT)m_logicalW;
    vp.Height = (FLOAT)m_logicalH;
    vp.MinDepth = 0; vp.MaxDepth = 1;

    ctx->OMSetRenderTargets(1, &m_realBBRTV, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(m_compositeRS);
    ctx->OMSetBlendState(m_compositeBlend, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_compositeDSS, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* nullVB = nullptr; UINT zero = 0;
    ctx->IASetVertexBuffers(0, 1, &nullVB, &zero, &zero);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->VSSetShader(m_compositeVS, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 2, srvs);
    ctx->PSSetSamplers(0, 1, &m_compositeSampler);
    ctx->Draw(3, 0);

    // Drop our SRV bindings so the game's next frame doesn't read from
    // a stale slot. RTV/blend/DSS will all be replaced by game's first
    // OMSet/blend/DSS calls so we leave them.
    ID3D11ShaderResourceView* nullSRV[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(0, 2, nullSRV);

    ctx->Release();
    NVDM_TRACE_FIRST_N(2, "  RunCompositePass: %s eyes (swap=%d) -> realBB rtv=%p\n",
                       topBottom ? "T-B" : "SBS", (int)swap, m_realBBRTV);
    return true;
}

void SwapChainProxy::CaptureAndPresentBlit()
{
    if (!m_shadowBB || !m_real || !m_parent) return;

    // What's currently in the shadow is the latest-eye render. Capture
    // it into the appropriate eye slot so the composite pass / fallback
    // blit can see both eyes.
    int currentEye = NvDirectMode::GetActiveEye();
    if (currentEye == NvDirectMode::kEyeLeft || currentEye == NvDirectMode::kEyeRight)
    {
        CaptureEye(currentEye);   // self-allocates the slot on first call
        m_lastSeenEye = currentEye;
    }

    // Stage 4b: if both eyes are captured AND shaders compile, run the
    // SBS / T-B composite shader pass writing directly to the real BB.
    if (RunCompositePass())
        return;

    // Fallback: only one eye captured (or shaders unavailable). Blit
    // the single-eye captured frame, falling back through SwapEyes
    // preference and finally the shadow itself for fully-mono games.
    ID3D11Texture2D* fallbackBB = nullptr;
    if (FAILED(m_real->GetBuffer(0, IID_ID3D11Texture2D,
                                  reinterpret_cast<void**>(&fallbackBB))) || !fallbackBB)
        return;

    ID3D11DeviceContext* ctx = nullptr;
    if (m_parent->GetReal()) m_parent->GetReal()->GetImmediateContext(&ctx);
    if (!ctx) { fallbackBB->Release(); return; }

    bool swap = NvDM_SwapEyes() != 0;
    ID3D11Texture2D* leftSrc  = swap ? m_rightEyeFrame : m_leftEyeFrame;
    ID3D11Texture2D* rightSrc = swap ? m_leftEyeFrame  : m_rightEyeFrame;
    ID3D11Texture2D* displaySrc = leftSrc ? leftSrc : (rightSrc ? rightSrc : m_shadowBB);

    ctx->CopyResource(fallbackBB, displaySrc);
    ctx->Release();

    NVDM_TRACE_FIRST_N(4, "  CaptureAndPresentBlit (fallback): currentEye=%d displaySrc=%s (%p) -> realBB=%p\n",
                       currentEye,
                       (displaySrc == m_leftEyeFrame  ? "leftEye"  :
                        displaySrc == m_rightEyeFrame ? "rightEye" : "shadow"),
                       displaySrc, fallbackBB);

    fallbackBB->Release();
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
    // Real BB pointer becomes invalid after ResizeBuffers; drop our RTV
    // and SRVs (eye frames may also need re-allocation if format changed).
    if (m_realBBRTV) { m_realBBRTV->Release(); m_realBBRTV = nullptr; }
    ReleaseEyeFrames();
    ReleaseShadowBB();
    return m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

// NOTE: ResizeTarget / SetFullscreenState etc are inline passthroughs in
// the header — the real swap chain knows the same dimensions the game
// thinks it has now (no doubling), so no overrides needed.

} // namespace NvDirectMode
