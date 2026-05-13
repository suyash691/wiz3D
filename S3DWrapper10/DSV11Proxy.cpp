/* wiz3D - ID3D11DepthStencilView proxy implementation (Option B Stage 3a) */

#include "StdAfx.h"
#include "DSV11Proxy.h"
#include "Device11Proxy.h"
#include "proxy_factory.h"     // IID_wiz3D_DSV11Proxy
#include "AdapterFunctions.h"

#pragma comment(lib, "dxguid.lib")

namespace wiz3d
{

DSV11Proxy::DSV11Proxy(ID3D11DepthStencilView* realLeft, ID3D11DepthStencilView* realRight, Device11Proxy* parent)
    : m_realLeft(realLeft)
    , m_realRight(realRight)
    , m_parent(parent)
    , m_refs(1)
{
}

DSV11Proxy::~DSV11Proxy()
{
    if (m_realRight) { m_realRight->Release(); m_realRight = nullptr; }
    if (m_realLeft)  { m_realLeft->Release();  m_realLeft  = nullptr; }
}

ULONG STDMETHODCALLTYPE DSV11Proxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE DSV11Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown        ||
        riid == IID_ID3D11DeviceChild ||
        riid == IID_ID3D11View      ||
        riid == IID_ID3D11DepthStencilView)
    {
        *ppvObj = static_cast<ID3D11DepthStencilView*>(this);
        AddRef();
        return S_OK;
    }
    // Stage 3b: private identity IID for TryUnwrapDSV.
    if (riid == IID_wiz3D_DSV11Proxy)
    {
        *ppvObj = static_cast<IUnknown*>(static_cast<ID3D11DepthStencilView*>(this));
        AddRef();
        return S_OK;
    }
    return m_realLeft->QueryInterface(riid, ppvObj);
}

void STDMETHODCALLTYPE DSV11Proxy::GetDevice(ID3D11Device** ppDevice)
{
    if (!ppDevice) return;
    if (m_parent)
    {
        *ppDevice = static_cast<ID3D11Device*>(m_parent);
        m_parent->AddRef();
        return;
    }
    m_realLeft->GetDevice(ppDevice);
}

} // namespace wiz3d
