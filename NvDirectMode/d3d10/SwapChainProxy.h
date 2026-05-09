/* NvDirectMode d3d10 - IDXGISwapChain proxy (parented to Device10Proxy)
 *
 * Stage 3 + 4 mirror of the d3d11 SwapChainProxy:
 *   - Real BB stays at game's logical (one-eye) size; doubled per-eye
 *     rendering surface is allocated as a side ID3D10Texture2D ("shadow")
 *     and handed back to the game from GetBuffer(0)
 *   - Per-eye capture via NvApiProxy's Wiz3D_SetEyeChangeCallback bridge
 *     — on each Stereo_SetActiveEye change, copy current shadow into the
 *     OLD eye's texture before the new render starts overwriting
 *   - At Present: capture current shadow as latest eye, then run a
 *     fullscreen-triangle shader pass that composites both eyes into
 *     the real BB as SBS or T-B per OutputMode config
 *   - Single-eye fallback when only one eye captured / shaders unavailable
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <d3d10.h>

namespace NvDirectMode
{

class Device10Proxy;

class SwapChainProxy : public IDXGISwapChain
{
public:
    explicit SwapChainProxy(IDXGISwapChain* real, Device10Proxy* parent);
    virtual ~SwapChainProxy();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (r == 0) { delete this; }
        return (ULONG)r;
    }

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override        { return m_real->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override       { return m_real->SetPrivateDataInterface(Name, pUnknown); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override            { return m_real->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override                               { return m_real->GetParent(riid, ppParent); }

    // IDXGIDeviceSubObject
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override                               { return m_real->GetDevice(riid, ppDevice); }

    // IDXGISwapChain
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override             { return m_real->SetFullscreenState(Fullscreen, pTarget); }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override         { return m_real->GetFullscreenState(pFullscreen, ppTarget); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override                                  { return m_real->GetDesc(pDesc); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override              { return m_real->ResizeTarget(pNewTargetParameters); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override                           { return m_real->GetContainingOutput(ppOutput); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override                     { return m_real->GetFrameStatistics(pStats); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override                          { return m_real->GetLastPresentCount(pLastPresentCount); }

    IDXGISwapChain* GetReal() const { return m_real; }
    Device10Proxy*  GetParent() const { return m_parent; }

    // Stage 4 callback target — the eye-change callback dispatcher invokes
    // this to capture the current shadow into the OLD eye's frame texture
    // before the next eye's render starts overwriting.
    void CaptureEye(int eyeBeingLeft);

private:
    void EnsureShadowBB();
    void ReleaseShadowBB();
    void ReleaseEyeFrames();
    bool EnsureCompositeShaders();
    void ReleaseCompositePipeline();
    bool RunCompositePass();
    void CaptureAndPresentBlit();

    IDXGISwapChain* m_real;
    Device10Proxy*  m_parent;
    LONG            m_refs;

    // Shadow + per-eye + composite (mirror of d3d11)
    ID3D10Texture2D* m_shadowBB;
    UINT             m_logicalW;
    UINT             m_logicalH;
    DXGI_FORMAT      m_shadowFormat;

    ID3D10Texture2D* m_leftEyeFrame;
    ID3D10Texture2D* m_rightEyeFrame;
    int              m_lastSeenEye;

    ID3D10VertexShader*       m_compositeVS;
    ID3D10PixelShader*        m_compositePS_SBS;
    ID3D10PixelShader*        m_compositePS_TB;
    ID3D10SamplerState*       m_compositeSampler;
    ID3D10RasterizerState*    m_compositeRS;
    ID3D10BlendState*         m_compositeBlend;
    ID3D10DepthStencilState*  m_compositeDSS;
    ID3D10ShaderResourceView* m_leftEyeSRV;
    ID3D10ShaderResourceView* m_rightEyeSRV;
    ID3D10RenderTargetView*   m_realBBRTV;
};

} // namespace NvDirectMode
