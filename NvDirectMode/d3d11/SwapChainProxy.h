/* NvDirectMode - IDXGISwapChain / IDXGISwapChain1 proxy
 *
 * Stage 1b-i: wraps IDXGISwapChain (back-buffer doubling lands in 1b-iii,
 * BB-pointer tracking that 1b-iv's OMSetRenderTargets routing needs hangs
 * off this proxy).
 *
 * Stage 2 Half 1: extended to IDXGISwapChain1 so the dxgi.dll factory
 * wrap can return a typed wrapper for `IDXGIFactory2::CreateSwapChainForHwnd`
 * et al. The IDXGISwapChain1-level methods passthrough to the real
 * IDXGISwapChain1 obtained via QI at construction time; if the real swap
 * chain doesn't expose IDXGISwapChain1 (Win7 without platform update), our
 * QI returns E_NOINTERFACE for that level so the game falls back to the
 * IDXGISwapChain interface (which we always wrap).
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>

namespace NvDirectMode
{

class Device11Proxy;

class SwapChainProxy : public IDXGISwapChain1
{
public:
    explicit SwapChainProxy(IDXGISwapChain* real, Device11Proxy* parent);
    virtual ~SwapChainProxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (r == 0)
        {
            if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
            if (m_real)  { m_real->Release();  m_real  = nullptr; }
            delete this;
        }
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
    HRESULT STDMETHODCALLTYPE ResizeBuffers(
        UINT BufferCount, UINT Width, UINT Height,
        DXGI_FORMAT NewFormat, UINT SwapChainFlags) override
    {
        return m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override              { return m_real->ResizeTarget(pNewTargetParameters); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override                           { return m_real->GetContainingOutput(ppOutput); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override                     { return m_real->GetFrameStatistics(pStats); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override                          { return m_real->GetLastPresentCount(pLastPresentCount); }

    // IDXGISwapChain1
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) override                                { return m_real1 ? m_real1->GetDesc1(pDesc) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override             { return m_real1 ? m_real1->GetFullscreenDesc(pDesc) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* pHwnd) override                                                  { return m_real1 ? m_real1->GetHwnd(pHwnd) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** ppUnk) override                              { return m_real1 ? m_real1->GetCoreWindow(riid, ppUnk) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT Flags,
                                       const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
    BOOL    STDMETHODCALLTYPE IsTemporaryMonoSupported() override                                            { return m_real1 ? m_real1->IsTemporaryMonoSupported() : FALSE; }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) override                 { return m_real1 ? m_real1->GetRestrictToOutput(ppRestrictToOutput) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* pColor) override                           { return m_real1 ? m_real1->SetBackgroundColor(pColor) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* pColor) override                                 { return m_real1 ? m_real1->GetBackgroundColor(pColor) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override                              { return m_real1 ? m_real1->SetRotation(Rotation) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* pRotation) override                            { return m_real1 ? m_real1->GetRotation(pRotation) : E_NOINTERFACE; }

    // Accessors for 1b-iii / 1b-iv
    IDXGISwapChain* GetReal() const { return m_real; }
    Device11Proxy*  GetParent() const { return m_parent; }

private:
    IDXGISwapChain*  m_real;
    IDXGISwapChain1* m_real1;   // null if real doesn't expose IDXGISwapChain1
    Device11Proxy*   m_parent;  // not AddRef'd; device outlives swap chain
    LONG             m_refs;
};

} // namespace NvDirectMode
