#include "DXGIDeviceProxy.h"
#include "Device11Proxy.h"
#include "DXGIAdapterProxy.h"
#include "log.h"

#include <d3d11.h>

#pragma comment(lib, "dxguid.lib")

namespace NvDirectMode
{

DXGIDeviceProxy::DXGIDeviceProxy(Device11Proxy* parent,
                                 IDXGIDevice*  r0,
                                 IDXGIDevice1* r1,
                                 IDXGIDevice2* r2,
                                 IDXGIDevice3* r3)
    : m_parent(parent)
    , m_real0(r0), m_real1(r1), m_real2(r2), m_real3(r3)
    , m_highestVer(r3 ? 3 : r2 ? 2 : r1 ? 1 : 0)
    , m_refs(1)
{
    LOG_VERBOSE("  DXGIDeviceProxy ctor: parent=%p real0=%p real1=%p real2=%p real3=%p (highestVer=%d)\n",
                parent, r0, r1, r2, r3, m_highestVer);
}

DXGIDeviceProxy::~DXGIDeviceProxy()
{
    // Each non-null pointer was obtained via QueryInterface on the real
    // device family — independent refs that all have to be released.
    if (m_real3) { m_real3->Release(); m_real3 = nullptr; }
    if (m_real2) { m_real2->Release(); m_real2 = nullptr; }
    if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
    if (m_real0) { m_real0->Release(); m_real0 = nullptr; }
}

ULONG STDMETHODCALLTYPE DXGIDeviceProxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE DXGIDeviceProxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;

    // The COM-identity loop fix: if the game asks for the underlying
    // ID3D11Device family, return our Device11Proxy so subsequent calls
    // route through the wrapper.
    if (riid == __uuidof(ID3D11Device))
    {
        if (!m_parent) return E_NOINTERFACE;
        return m_parent->QueryInterface(riid, ppvObj);
    }

    // Self-claim our own interface family.
    if (riid == IID_IUnknown ||
        riid == IID_IDXGIObject ||
        riid == IID_IDXGIDeviceSubObject ||
        riid == IID_IDXGIDevice)
    {
        *ppvObj = static_cast<IDXGIDevice*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIDevice1 && m_real1)
    {
        *ppvObj = static_cast<IDXGIDevice1*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIDevice2 && m_real2)
    {
        *ppvObj = static_cast<IDXGIDevice2*>(this);
        AddRef();
        return S_OK;
    }
    if (riid == IID_IDXGIDevice3 && m_real3)
    {
        *ppvObj = static_cast<IDXGIDevice3*>(this);
        AddRef();
        return S_OK;
    }

    // Anything else (IDXGIDevice4, ID3D11Device1+, vendor IIDs) — passthrough
    // unwrapped. Identity won't be preserved for those riids; a future stage
    // can extend coverage as games need it.
    HRESULT hr = m_real0->QueryInterface(riid, ppvObj);
    NVDM_TRACE_FIRST_N(8,
        "  DXGIDeviceProxy::QI(unknown IID) hr=0x%08lX -- bypass risk\n", hr);
    return hr;
}

// Helper used by GetAdapter and GetParent(IDXGIAdapter*): take a real
// IDXGIAdapter, QI for the IDXGIAdapter1 face if available, hand both
// refs to a fresh DXGIAdapterProxy.
static IDXGIAdapter* WrapRealAdapter(IDXGIAdapter* realAdapter)
{
    if (!realAdapter) return nullptr;
    IDXGIAdapter1* r1 = nullptr;
    realAdapter->QueryInterface(IID_IDXGIAdapter1, reinterpret_cast<void**>(&r1));
    auto* proxy = new DXGIAdapterProxy(realAdapter, r1);
    return static_cast<IDXGIAdapter*>(proxy);
}

HRESULT STDMETHODCALLTYPE DXGIDeviceProxy::GetAdapter(IDXGIAdapter** ppAdapter)
{
    if (!ppAdapter) return E_POINTER;
    IDXGIAdapter* realAdapter = nullptr;
    HRESULT hr = m_real0->GetAdapter(&realAdapter);
    if (FAILED(hr) || !realAdapter) { *ppAdapter = nullptr; return hr; }
    *ppAdapter = WrapRealAdapter(realAdapter);
    NVDM_TRACE_FIRST_N(4, "  DXGIDeviceProxy::GetAdapter: wrapped real=%p -> %p\n",
                       (void*)realAdapter, (void*)*ppAdapter);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DXGIDeviceProxy::GetParent(REFIID riid, void** ppParent)
{
    if (!ppParent) return E_POINTER;

    // For adapter queries: route through our DXGIAdapterProxy so that a
    // game walking device → adapter → factory hits our wrapped factory
    // when it eventually creates its swap chain.
    if (riid == IID_IDXGIAdapter || riid == IID_IDXGIAdapter1)
    {
        IDXGIAdapter* realAdapter = nullptr;
        HRESULT hr = m_real0->GetParent(IID_IDXGIAdapter, reinterpret_cast<void**>(&realAdapter));
        if (FAILED(hr) || !realAdapter) { *ppParent = nullptr; return hr; }
        IDXGIAdapter* wrapped = WrapRealAdapter(realAdapter);
        if (riid == IID_IDXGIAdapter1)
        {
            // Round-trip via QI to get the IDXGIAdapter1 face with proper
            // refcount; drop our IDXGIAdapter ref afterwards.
            HRESULT qiHr = wrapped->QueryInterface(IID_IDXGIAdapter1, ppParent);
            wrapped->Release();
            return qiHr;
        }
        *ppParent = wrapped;   // transfer our ref to the caller
        return S_OK;
    }

    return m_real0->GetParent(riid, ppParent);
}

} // namespace NvDirectMode
