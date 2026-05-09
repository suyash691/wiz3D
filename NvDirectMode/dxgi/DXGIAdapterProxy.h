/* NvDirectMode/dxgi - IDXGIAdapter / IDXGIAdapter1 proxy
 *
 * Mirror of d3d11.dll's DXGIAdapterProxy, but lives in dxgi.dll so that
 * adapters returned by IDXGIFactory::EnumAdapters / EnumAdapters1 can
 * be wrapped without a cross-DLL bridge call (the d3d11 version is
 * reached via DXGIDeviceProxy::GetAdapter from a different code path).
 *
 * Both copies share the same vendor-spoof identity (NvDirectMode/spoof_identity.h)
 * so games querying via either path see the same NVIDIA RTX 2080 Ti.
 *
 * GetParent(IDXGIFactory*) wraps locally via DXGIFactoryProxy (no cross-
 * DLL call needed since both classes are in the same DLL).
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

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override   { return m_real0->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnk) override      { return m_real0->SetPrivateDataInterface(Name, pUnk); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override       { return m_real0->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;

    // IDXGIAdapter
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT Output, IDXGIOutput** ppOutput) override                 { return m_real0->EnumOutputs(Output, ppOutput); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID InterfaceName, LARGE_INTEGER* pUMDVersion) override
                                                                                                        { return m_real0->CheckInterfaceSupport(InterfaceName, pUMDVersion); }

    // IDXGIAdapter1
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* pDesc) override;

private:
    IDXGIAdapter*  m_real0;
    IDXGIAdapter1* m_real1;
    LONG           m_refs;
};

// Public factory entry: wrap a real IDXGIAdapter in DXGIAdapterProxy
// (QI'ing for IDXGIAdapter1 if available). Used by DXGIFactoryProxy's
// EnumAdapters / EnumAdapters1 overrides.
IDXGIAdapter* WrapRealAdapter(IDXGIAdapter* realAdapter);

} // namespace NvDirectMode
