#include "DXGIAdapterProxy.h"
#include "DXGIFactoryProxy.h"
#include "../spoof_identity.h"

#include <wchar.h>

#pragma comment(lib, "dxguid.lib")

extern "C" void NvDM_DxgiLog(const char* fmt, ...);

namespace NvDirectMode
{

namespace
{
    void ApplySpoofToDesc(DXGI_ADAPTER_DESC* d)
    {
        if (!d) return;
        wcsncpy_s(d->Description, sizeof(d->Description) / sizeof(WCHAR),
                  kSpoofGpuNameW, _TRUNCATE);
        d->VendorId = kSpoofPciVendor;
        d->DeviceId = kSpoofPciDevice;
        d->SubSysId = kSpoofSubSysId;
        d->Revision = kSpoofRevision;
    }
    void ApplySpoofToDesc1(DXGI_ADAPTER_DESC1* d)
    {
        if (!d) return;
        wcsncpy_s(d->Description, sizeof(d->Description) / sizeof(WCHAR),
                  kSpoofGpuNameW, _TRUNCATE);
        d->VendorId = kSpoofPciVendor;
        d->DeviceId = kSpoofPciDevice;
        d->SubSysId = kSpoofSubSysId;
        d->Revision = kSpoofRevision;
    }
}

DXGIAdapterProxy::DXGIAdapterProxy(IDXGIAdapter* r0, IDXGIAdapter1* r1)
    : m_real0(r0), m_real1(r1), m_refs(1)
{
    NvDM_DxgiLog("  dxgi/DXGIAdapterProxy ctor: real0=%p real1=%p\n", r0, r1);
}

DXGIAdapterProxy::~DXGIAdapterProxy()
{
    if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
    if (m_real0) { m_real0->Release(); m_real0 = nullptr; }
}

ULONG STDMETHODCALLTYPE DXGIAdapterProxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown ||
        riid == IID_IDXGIObject ||
        riid == IID_IDXGIAdapter)
    {
        *ppvObj = static_cast<IDXGIAdapter*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIAdapter1 && m_real1)
    {
        *ppvObj = static_cast<IDXGIAdapter1*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::GetDesc(DXGI_ADAPTER_DESC* pDesc)
{
    HRESULT hr = m_real0->GetDesc(pDesc);
    if (SUCCEEDED(hr) && pDesc)
    {
        ApplySpoofToDesc(pDesc);
        NvDM_DxgiLog("  dxgi/DXGIAdapterProxy::GetDesc: spoofed as NVIDIA RTX 2080 Ti (vendor=0x%04X)\n",
                     pDesc->VendorId);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::GetDesc1(DXGI_ADAPTER_DESC1* pDesc)
{
    if (!m_real1) return E_NOINTERFACE;
    HRESULT hr = m_real1->GetDesc1(pDesc);
    if (SUCCEEDED(hr) && pDesc)
    {
        ApplySpoofToDesc1(pDesc);
        NvDM_DxgiLog("  dxgi/DXGIAdapterProxy::GetDesc1: spoofed as NVIDIA RTX 2080 Ti (vendor=0x%04X)\n",
                     pDesc->VendorId);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DXGIAdapterProxy::GetParent(REFIID riid, void** ppParent)
{
    if (!ppParent) return E_POINTER;
    if (riid == IID_IDXGIFactory || riid == IID_IDXGIFactory1 || riid == IID_IDXGIFactory2)
    {
        IUnknown* realFactory = nullptr;
        HRESULT hr = m_real0->GetParent(riid, reinterpret_cast<void**>(&realFactory));
        if (FAILED(hr) || !realFactory) return hr;
        IUnknown* wrapped = WrapRealFactoryAsRequested(realFactory, riid);
        if (wrapped) { *ppParent = wrapped; return S_OK; }
        *ppParent = realFactory;
        return S_OK;
    }
    return m_real0->GetParent(riid, ppParent);
}

IDXGIAdapter* WrapRealAdapter(IDXGIAdapter* realAdapter)
{
    if (!realAdapter) return nullptr;
    IDXGIAdapter1* r1 = nullptr;
    realAdapter->QueryInterface(IID_IDXGIAdapter1, reinterpret_cast<void**>(&r1));
    auto* proxy = new DXGIAdapterProxy(realAdapter, r1);
    return static_cast<IDXGIAdapter*>(proxy);
}

} // namespace NvDirectMode
