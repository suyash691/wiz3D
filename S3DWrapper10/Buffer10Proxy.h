/* wiz3D - ID3D10Buffer proxy (Option B for DX10, Stage 3 port) */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>

namespace wiz3d
{

class Device10Proxy;

class Buffer10Proxy : public ID3D10Buffer
{
public:
    Buffer10Proxy(ID3D10Buffer* real, Device10Proxy* parent);
    virtual ~Buffer10Proxy();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    void    STDMETHODCALLTYPE GetDevice(ID3D10Device** ppDevice) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override                  { return m_real->GetPrivateData(guid, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override              { return m_real->SetPrivateData(guid, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override                { return m_real->SetPrivateDataInterface(guid, pData); }

    void    STDMETHODCALLTYPE GetType(D3D10_RESOURCE_DIMENSION* pResourceDimension) override                       { m_real->GetType(pResourceDimension); }
    void    STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) override                                  { m_real->SetEvictionPriority(EvictionPriority); }
    UINT    STDMETHODCALLTYPE GetEvictionPriority() override                                                       { return m_real->GetEvictionPriority(); }

    HRESULT STDMETHODCALLTYPE Map(D3D10_MAP MapType, UINT MapFlags, void** ppData) override;
    void    STDMETHODCALLTYPE Unmap() override;
    void    STDMETHODCALLTYPE GetDesc(D3D10_BUFFER_DESC* pDesc) override                                           { m_real->GetDesc(pDesc); }

    ID3D10Buffer*  GetReal()       const { return m_real;   }
    Device10Proxy* GetParent()     const { return m_parent; }

    bool IsVSBound() const { return m_vsBound; }
    void TagVSBound()      { m_vsBound = true; }

private:
    ID3D10Buffer*  m_real;
    Device10Proxy* m_parent;
    LONG           m_refs;
    bool           m_vsBound;

    // Stage 4c carryover: tracks an in-progress write Map so Unmap can
    // snapshot the bytes the game wrote and push a per-eye replay closure
    // onto the device's m_frameCommands.
    bool      m_activeMapValid;
    void*     m_activeMapData;
    UINT      m_activeMapByteWidth;
    D3D10_MAP m_activeMapType;
};

} // namespace wiz3d
