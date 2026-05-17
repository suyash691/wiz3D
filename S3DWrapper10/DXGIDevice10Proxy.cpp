/* wiz3D - DXGIDevice10Proxy implementation */

#include "StdAfx.h"
#include "DXGIDevice10Proxy.h"
#include "Device10Proxy.h"
#include "AdapterFunctions.h"   // DDILog

#include <d3d10.h>

#pragma comment(lib, "dxguid.lib")

namespace wiz3d
{

DXGIDevice10Proxy::DXGIDevice10Proxy(Device10Proxy* parent,
                                     IDXGIDevice*  r0,
                                     IDXGIDevice1* r1,
                                     IDXGIDevice2* r2,
                                     IDXGIDevice3* r3)
    : m_parent(parent)
    , m_real0(r0), m_real1(r1), m_real2(r2), m_real3(r3)
    , m_refs(1)
{
    DDILog("  DXGIDevice10Proxy ctor: parent=%p real0=%p real1=%p real2=%p real3=%p\n",
           parent, r0, r1, r2, r3);
}

DXGIDevice10Proxy::~DXGIDevice10Proxy()
{
    if (m_real3) { m_real3->Release(); m_real3 = nullptr; }
    if (m_real2) { m_real2->Release(); m_real2 = nullptr; }
    if (m_real1) { m_real1->Release(); m_real1 = nullptr; }
    if (m_real0) { m_real0->Release(); m_real0 = nullptr; }
}

ULONG STDMETHODCALLTYPE DXGIDevice10Proxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE DXGIDevice10Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;

    // The DX10 identity loop fix: QI(ID3D10Device) routes back to our
    // Device10Proxy. This is what makes the factory hook's
    // QI(IID_wiz3D_Device10Proxy) succeed for the two-call CreateSwapChain
    // path (FC2 / JC2 / De Blob / Lost Planet etc.).
    if (riid == __uuidof(ID3D10Device))
    {
        if (!m_parent) return E_NOINTERFACE;
        return m_parent->QueryInterface(riid, ppvObj);
    }

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

    return m_real0->QueryInterface(riid, ppvObj);
}

} // namespace wiz3d
