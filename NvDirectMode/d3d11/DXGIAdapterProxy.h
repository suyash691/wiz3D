/* NvDirectMode - IDXGIAdapter / IDXGIAdapter1 proxy
 *
 * Stage-after-4 fix for HD3D-style games (Dirt Rally, Hitman Absolution,
 * etc.) whose swap chain creation never went through our wrap. Logs
 * showed games getting wrapped Device11Proxy + DXGIDeviceProxy + wrapped
 * factory, but no IDXGIFactory::CreateSwapChain ever fired on us — the
 * games walk a different COM chain to reach the factory:
 *
 *     device->QI(IDXGIDevice)        // -> our DXGIDeviceProxy
 *     dxgiDev->GetAdapter(&adapter)   // -> currently passthrough = REAL adapter
 *     adapter->GetParent(IDXGIFactory)// -> REAL factory (unwrapped)
 *     realFactory->CreateSwapChain    // bypasses our hooks entirely
 *
 * DXGIAdapterProxy plugs the second step. DXGIDeviceProxy::GetAdapter
 * (and ::GetParent for IDXGIAdapter*) now return a wrapped adapter; the
 * adapter's GetParent(IDXGIFactory*) calls back into dxgi.dll's
 * NvDM_DXGI_WrapFactory bridge so the game's CreateSwapChain* lands on
 * our DXGIFactoryProxy. With both ends of the COM walk wrapped, a game
 * that walks Device → IDXGIDevice → Adapter → Factory ends up at our
 * factory regardless of which path it takes.
 *
 * If dxgi.dll proxy isn't loaded in this process (e.g. game ships
 * without it), GetParent falls back to handing the unwrapped real
 * factory to the game (same as before this proxy existed).
 *
 * QI policy mirrors DXGIDeviceProxy: claim IDXGIAdapter / IDXGIAdapter1
 * ourselves, return E_NOINTERFACE for IDXGIAdapter2+ so games fall back
 * to a level we wrap rather than escaping with an unwrapped pointer.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>

namespace NvDirectMode
{

class DXGIAdapterProxy : public IDXGIAdapter1
{
public:
    DXGIAdapterProxy(IDXGIAdapter* r0, IDXGIAdapter1* r1);
    virtual ~DXGIAdapterProxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override   { return m_real0->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnk) override      { return m_real0->SetPrivateDataInterface(Name, pUnk); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override       { return m_real0->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;   // wraps if asked for a factory

    // IDXGIAdapter
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT Output, IDXGIOutput** ppOutput) override                 { return m_real0->EnumOutputs(Output, ppOutput); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* pDesc) override                                { return m_real0->GetDesc(pDesc); }
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID InterfaceName, LARGE_INTEGER* pUMDVersion) override
                                                                                                        { return m_real0->CheckInterfaceSupport(InterfaceName, pUMDVersion); }

    // IDXGIAdapter1
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* pDesc) override                              { return m_real1 ? m_real1->GetDesc1(pDesc) : E_NOINTERFACE; }

private:
    IDXGIAdapter*  m_real0;
    IDXGIAdapter1* m_real1;   // null if real doesn't expose IDXGIAdapter1
    LONG           m_refs;
};

} // namespace NvDirectMode
