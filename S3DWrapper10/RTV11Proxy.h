/* wiz3D - ID3D11RenderTargetView proxy (Option B Stage 3a)
 *
 * Wraps an RTV returned from Device11Proxy::CreateRenderTargetView. When the
 * underlying resource (passed at construction) is a Texture2D11Proxy whose
 * IsStereo() returns true, the constructor also creates a right-eye RTV via
 * the real device using the proxy's m_realRight texture. Stage 4's
 * OMSetRenderTargets routing reads IsStereo() to know which RTVs need
 * per-eye binding.
 *
 * Mirrors S3DWrapper10/RenderTargetViewWrapper.cpp at the COM layer.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

namespace wiz3d
{

class Device11Proxy;

class RTV11Proxy : public ID3D11RenderTargetView
{
public:
    // realLeft is the always-present RTV from the game's request; realRight
    // is the optional sibling view created from the stereo resource's right
    // texture (nullable). Parent Device11Proxy is used by GetDevice and
    // GetResource to keep COM identity flowing through us.
    RTV11Proxy(ID3D11RenderTargetView* realLeft, ID3D11RenderTargetView* realRight, Device11Proxy* parent);
    virtual ~RTV11Proxy();

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
    void    STDMETHODCALLTYPE GetResource(ID3D11Resource** ppResource) override;

    // ID3D11RenderTargetView
    void    STDMETHODCALLTYPE GetDesc(D3D11_RENDER_TARGET_VIEW_DESC* pDesc) override                               { m_realLeft->GetDesc(pDesc); }

    // wiz3D accessors
    bool                    IsStereo()      const { return m_realRight != nullptr; }
    ID3D11RenderTargetView* GetReal()       const { return m_realLeft;  }
    ID3D11RenderTargetView* GetRealRight()  const { return m_realRight; }
    Device11Proxy*          GetParent()     const { return m_parent;    }

private:
    ID3D11RenderTargetView* m_realLeft;
    ID3D11RenderTargetView* m_realRight;  // nullable
    Device11Proxy*          m_parent;
    LONG                    m_refs;
};

} // namespace wiz3d
