/* wiz3D - ID3D11DepthStencilView proxy (Option B Stage 3a)
 *
 * Same pattern as RTV11Proxy but for depth-stencil views. Mirrors
 * S3DWrapper10/DepthStencilViewWrapper.cpp at the COM layer.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

namespace wiz3d
{

class Device11Proxy;

class DSV11Proxy : public ID3D11DepthStencilView
{
public:
    DSV11Proxy(ID3D11DepthStencilView* realLeft, ID3D11DepthStencilView* realRight, Device11Proxy* parent);
    virtual ~DSV11Proxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    // ID3D11DeviceChild
    void    STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override                  { return m_realLeft->GetPrivateData(guid, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override              { return m_realLeft->SetPrivateData(guid, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override                { return m_realLeft->SetPrivateDataInterface(guid, pData); }

    // ID3D11View
    void    STDMETHODCALLTYPE GetResource(ID3D11Resource** ppResource) override                                    { if (ppResource) m_realLeft->GetResource(ppResource); }

    // ID3D11DepthStencilView
    void    STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc) override                               { m_realLeft->GetDesc(pDesc); }

    // wiz3D accessors
    bool                    IsStereo()      const { return m_realRight != nullptr; }
    ID3D11DepthStencilView* GetReal()       const { return m_realLeft;  }
    ID3D11DepthStencilView* GetRealRight()  const { return m_realRight; }
    Device11Proxy*          GetParent()     const { return m_parent;    }

private:
    ID3D11DepthStencilView* m_realLeft;
    ID3D11DepthStencilView* m_realRight;  // nullable
    Device11Proxy*          m_parent;
    LONG                    m_refs;
};

} // namespace wiz3d
