/* NvDirectMode - IDXGIFactory / IDXGIFactory1 / IDXGIFactory2 proxy
 *
 * Stage 2 Half 1: wraps the IDXGIFactory family returned from
 * CreateDXGIFactory / CreateDXGIFactory1 / CreateDXGIFactory2 so we can
 * intercept all four swap-chain creation methods on the factory path:
 *
 *   IDXGIFactory ::CreateSwapChain               (legacy DESC)
 *   IDXGIFactory2::CreateSwapChainForHwnd        (DESC1 + fullscreen DESC)
 *   IDXGIFactory2::CreateSwapChainForCoreWindow  (DESC1, UWP)
 *   IDXGIFactory2::CreateSwapChainForComposition (DESC1, DComp)
 *
 * The d3d11.dll proxy alone misses these — production DX11 games (Tomb
 * Raider 2013, FC2, JC2, ...) typically create their swap chain via
 * CreateDXGIFactory + IDXGIFactory::CreateSwapChain rather than the
 * legacy D3D11CreateDeviceAndSwapChain path the d3d11 proxy already wraps.
 *
 * Cross-DLL hookup: each intercepted method doubles the desc, calls the
 * real factory, then asks d3d11.dll's NvDM_WrapAndRegisterSwapChain (via
 * GetProcAddress) to wrap the resulting swap chain in a SwapChainProxy.
 * If d3d11.dll isn't loaded (no NvDirectMode d3d11 proxy in the game) the
 * bridge returns null and we hand the unwrapped real swap chain back to
 * the game (passthrough — consistent with stage 1 behaviour).
 *
 * QI policy: claim only IDXGIFactory / IDXGIFactory1 / IDXGIFactory2 levels
 * we have (refer to the real). For IDXGIFactory3+ return E_NOINTERFACE so
 * the game falls back to IDXGIFactory2 (which we wrap). Same trade-off as
 * SwapChainProxy: avoid the COM-identity escape where the game gets an
 * unwrapped IDXGIFactory3+ and bypasses our CreateSwapChain* hooks.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>

namespace NvDirectMode
{

class DXGIFactoryProxy : public IDXGIFactory2
{
public:
    DXGIFactoryProxy(IDXGIFactory*  r0,
                     IDXGIFactory1* r1,
                     IDXGIFactory2* r2);
    virtual ~DXGIFactoryProxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override                  { return m_real0->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override                 { return m_real0->SetPrivateDataInterface(Name, pUnknown); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override                      { return m_real0->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override                                          { return m_real0->GetParent(riid, ppParent); }

    // IDXGIFactory
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) override                            { return m_real0->EnumAdapters(Adapter, ppAdapter); }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override                            { return m_real0->MakeWindowAssociation(WindowHandle, Flags); }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* pWindowHandle) override                                        { return m_real0->GetWindowAssociation(pWindowHandle); }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* pDevice,
                                              DXGI_SWAP_CHAIN_DESC* pDesc,
                                              IDXGISwapChain** ppSwapChain) override;  // INTERCEPTED
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter) override                  { return m_real0->CreateSoftwareAdapter(Module, ppAdapter); }

    // IDXGIFactory1
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) override                          { return m_real1 ? m_real1->EnumAdapters1(Adapter, ppAdapter) : E_NOINTERFACE; }
    BOOL    STDMETHODCALLTYPE IsCurrent() override                                                                      { return m_real1 ? m_real1->IsCurrent() : FALSE; }

    // IDXGIFactory2
    BOOL    STDMETHODCALLTYPE IsWindowedStereoEnabled() override                                                        { return m_real2 ? m_real2->IsWindowedStereoEnabled() : FALSE; }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(
        IUnknown* pDevice, HWND hWnd,
        const DXGI_SWAP_CHAIN_DESC1* pDesc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
        IDXGIOutput* pRestrictToOutput,
        IDXGISwapChain1** ppSwapChain) override;  // INTERCEPTED
    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(
        IUnknown* pDevice, IUnknown* pWindow,
        const DXGI_SWAP_CHAIN_DESC1* pDesc,
        IDXGIOutput* pRestrictToOutput,
        IDXGISwapChain1** ppSwapChain) override;  // INTERCEPTED
    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID* pLuid) override                      { return m_real2 ? m_real2->GetSharedResourceAdapterLuid(hResource, pLuid) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie) override       { return m_real2 ? m_real2->RegisterStereoStatusWindow(WindowHandle, wMsg, pdwCookie) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD* pdwCookie) override                       { return m_real2 ? m_real2->RegisterStereoStatusEvent(hEvent, pdwCookie) : E_NOINTERFACE; }
    void    STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) override                                           { if (m_real2) m_real2->UnregisterStereoStatus(dwCookie); }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND WindowHandle, UINT wMsg, DWORD* pdwCookie) override    { return m_real2 ? m_real2->RegisterOcclusionStatusWindow(WindowHandle, wMsg, pdwCookie) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD* pdwCookie) override                    { return m_real2 ? m_real2->RegisterOcclusionStatusEvent(hEvent, pdwCookie) : E_NOINTERFACE; }
    void    STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) override                                        { if (m_real2) m_real2->UnregisterOcclusionStatus(dwCookie); }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(
        IUnknown* pDevice,
        const DXGI_SWAP_CHAIN_DESC1* pDesc,
        IDXGIOutput* pRestrictToOutput,
        IDXGISwapChain1** ppSwapChain) override;  // INTERCEPTED

private:
    IDXGIFactory*  m_real0;
    IDXGIFactory1* m_real1;
    IDXGIFactory2* m_real2;
    LONG           m_refs;
};

// Public factory entry point — called from dllmain after each
// CreateDXGIFactory* invocation. Wraps the real factory in our proxy.
// Returns the wrapped IDXGIFactory* (cast to whatever IID the caller
// originally requested via riid).
IUnknown* WrapRealFactoryAsRequested(IUnknown* realFactory, REFIID riid);

} // namespace NvDirectMode
