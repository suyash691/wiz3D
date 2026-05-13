/* wiz3D - ID3D11RenderTargetView proxy implementation (Option B Stage 3a) */

#include "StdAfx.h"
#include "RTV11Proxy.h"
#include "Device11Proxy.h"
#include "Texture2D11Proxy.h"
#include "AdapterFunctions.h"

#pragma comment(lib, "dxguid.lib")

namespace wiz3d
{

RTV11Proxy::RTV11Proxy(ID3D11RenderTargetView* realLeft, ID3D11RenderTargetView* realRight, Device11Proxy* parent)
    : m_realLeft(realLeft)
    , m_realRight(realRight)
    , m_parent(parent)
    , m_refs(1)
{
}

RTV11Proxy::~RTV11Proxy()
{
    if (m_realRight) { m_realRight->Release(); m_realRight = nullptr; }
    if (m_realLeft)  { m_realLeft->Release();  m_realLeft  = nullptr; }
}

ULONG STDMETHODCALLTYPE RTV11Proxy::Release()
{
    LONG r = InterlockedDecrement(&m_refs);
    if (r == 0) delete this;
    return (ULONG)r;
}

HRESULT STDMETHODCALLTYPE RTV11Proxy::QueryInterface(REFIID riid, void** ppvObj)
{
    if (!ppvObj) return E_POINTER;
    if (riid == IID_IUnknown        ||
        riid == IID_ID3D11DeviceChild ||
        riid == IID_ID3D11View      ||
        riid == IID_ID3D11RenderTargetView)
    {
        *ppvObj = static_cast<ID3D11RenderTargetView*>(this);
        AddRef();
        return S_OK;
    }
    return m_realLeft->QueryInterface(riid, ppvObj);
}

void STDMETHODCALLTYPE RTV11Proxy::GetDevice(ID3D11Device** ppDevice)
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

void STDMETHODCALLTYPE RTV11Proxy::GetResource(ID3D11Resource** ppResource)
{
    // ID3D11View::GetResource is supposed to return the resource that the view
    // was created against. The game's view of the world is "I asked for an
    // RTV on a Texture2D11Proxy" — so we should hand back the Texture2D11Proxy,
    // not the unwrapped real texture. Game can then continue to operate on
    // the wrapped texture (and our identity tracking stays intact).
    //
    // For Stage 3a we don't yet have a reverse map from real-texture to
    // proxy-texture, so we forward to m_realLeft for now. Stage 4 needs a
    // lookup table on Device11Proxy to walk back; this comment marks the spot.
    if (!ppResource) return;
    m_realLeft->GetResource(ppResource);
}

} // namespace wiz3d
