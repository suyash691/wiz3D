/* wiz3D - IDXGISwapChain proxy for DX10 (Option B Stage 4d-equivalent)
 *
 * Mirror of SwapChain11Proxy but holding a Device10Proxy* parent and using
 * ID3D10* composite resources. The IDXGISwapChain interface itself is the
 * same for DX10 and DX11 (it's all DXGI), so the wrapper structure copies
 * directly; only the BB siblings, composite shader pipeline, and the parent
 * device differ.
 *
 * Drops IDXGISwapChain1 support — DX10 games predate that extension by
 * several years, so the additional 1+ surface area would be unused weight.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9types.h>   // D3DCOLORVALUE for the DXGI_RGBA shim below
#ifndef _DXGI_RGBA_DEFINED
#define _DXGI_RGBA_DEFINED
typedef D3DCOLORVALUE DXGI_RGBA;
#endif
#include <d3d10.h>
#include <dxgi.h>

namespace wiz3d
{

class Device10Proxy;
class Texture2D10Proxy;

class SwapChain10Proxy : public IDXGISwapChain
{
public:
    SwapChain10Proxy(IDXGISwapChain* real, Device10Proxy* parent);
    virtual ~SwapChain10Proxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override          { return m_real->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnk) override             { return m_real->SetPrivateDataInterface(Name, pUnk); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override              { return m_real->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override                                 { return m_real->GetParent(riid, ppParent); }

    // IDXGIDeviceSubObject
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;

    // IDXGISwapChain
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override;
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override           { return m_real->GetFullscreenState(pFullscreen, ppTarget); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override                                    { return m_real->GetDesc(pDesc); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override                { return m_real->ResizeTarget(pNewTargetParameters); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override                             { return m_real->GetContainingOutput(ppOutput); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override                       { return m_real->GetFrameStatistics(pStats); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override                            { return m_real->GetLastPresentCount(pLastPresentCount); }

    IDXGISwapChain* GetReal()    const { return m_real;   }
    Device10Proxy*  GetParent()  const { return m_parent; }

private:
    void    OnPresentBoundaryPre();
    void    OnPresentBoundaryPost();

    HRESULT EnsureStereoBackBuffer();
    HRESULT EnsureComposite();
    void    ReleaseStereoBackBuffer();
    void    ReleaseComposite();
    void    DoComposite();

    IDXGISwapChain*  m_real;
    Device10Proxy*   m_parent;
    LONG             m_refs;

    ID3D10Texture2D*          m_leftBB;
    ID3D10Texture2D*          m_rightBB;
    Texture2D10Proxy*         m_wrappedBB;
    ID3D10ShaderResourceView* m_leftSRV;
    ID3D10ShaderResourceView* m_rightSRV;
    ID3D10RenderTargetView*   m_realBBRTV;
    UINT                      m_bbWidth;
    UINT                      m_bbHeight;
    DXGI_FORMAT               m_bbFormat;

    ID3D10VertexShader*       m_compositeVS;
    ID3D10PixelShader*        m_compositePS;
    ID3D10SamplerState*       m_compositeSampler;
    ID3D10RasterizerState*    m_compositeRaster;
    ID3D10BlendState*         m_compositeBlend;
    ID3D10DepthStencilState*  m_compositeDepthStencil;
};

} // namespace wiz3d
