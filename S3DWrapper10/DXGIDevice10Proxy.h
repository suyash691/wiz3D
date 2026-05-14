/* wiz3D - IDXGIDevice / IDXGIDevice1/2/3 proxy for DX10
 *
 * Mirror of DXGIDeviceProxy (DX11) but for the DX10 path. Closes the same
 * COM-identity loop: when a DX10 game asks `dev->QI(IDXGIDevice) ->
 * QI(ID3D10Device)`, the round trip needs to land back on our
 * Device10Proxy rather than the real device. Without this, the runtime
 * passes the unwrapped real device pointer to `factory->CreateSwapChain`,
 * the factory hook's `QI(IID_wiz3D_Device10Proxy)` misses, and the swap
 * chain never gets wrapped — so DX10 games (FC2, JC2, De Blob, Lost
 * Planet) lose SBS even with UseCOMWrapSwapChain=1.
 *
 * Claims up to IDXGIDevice3 (Windows 8.1 era — same coverage as the DX11
 * sibling).
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9types.h>
#ifndef _DXGI_RGBA_DEFINED
#define _DXGI_RGBA_DEFINED
typedef D3DCOLORVALUE DXGI_RGBA;
#endif
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

namespace wiz3d
{

class Device10Proxy;

class DXGIDevice10Proxy : public IDXGIDevice3
{
public:
    DXGIDevice10Proxy(Device10Proxy* parent,
                      IDXGIDevice*  r0,
                      IDXGIDevice1* r1,
                      IDXGIDevice2* r2,
                      IDXGIDevice3* r3);
    virtual ~DXGIDevice10Proxy();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override   { return m_real0->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnk) override      { return m_real0->SetPrivateDataInterface(Name, pUnk); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override       { return m_real0->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override                          { return m_real0->GetParent(riid, ppParent); }

    HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** ppAdapter) override                             { return m_real0->GetAdapter(ppAdapter); }
    HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC* pDesc, UINT NumSurfaces,
                                            DXGI_USAGE Usage, const DXGI_SHARED_RESOURCE* pSharedResource,
                                            IDXGISurface** ppSurface) override                          { return m_real0->CreateSurface(pDesc, NumSurfaces, Usage, pSharedResource, ppSurface); }
    HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown* const* ppResources,
                                                     DXGI_RESIDENCY* pResidencyStatus, UINT NumResources) override
                                                                                                        { return m_real0->QueryResourceResidency(ppResources, pResidencyStatus, NumResources); }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT Priority) override                               { return m_real0->SetGPUThreadPriority(Priority); }
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* pPriority) override                             { return m_real0->GetGPUThreadPriority(pPriority); }

    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override                          { return m_real1 ? m_real1->SetMaximumFrameLatency(MaxLatency) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override                        { return m_real1 ? m_real1->GetMaximumFrameLatency(pMaxLatency) : E_NOINTERFACE; }

    HRESULT STDMETHODCALLTYPE OfferResources(UINT NumResources, IDXGIResource* const* ppResources,
                                             DXGI_OFFER_RESOURCE_PRIORITY Priority) override            { return m_real2 ? m_real2->OfferResources(NumResources, ppResources, Priority) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE ReclaimResources(UINT NumResources, IDXGIResource* const* ppResources,
                                               BOOL* pDiscarded) override                               { return m_real2 ? m_real2->ReclaimResources(NumResources, ppResources, pDiscarded) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE EnqueueSetEvent(HANDLE hEvent) override                                   { return m_real2 ? m_real2->EnqueueSetEvent(hEvent) : E_NOINTERFACE; }

    void    STDMETHODCALLTYPE Trim() override                                                           { if (m_real3) m_real3->Trim(); }

    void DetachParent() { m_parent = nullptr; }

private:
    Device10Proxy* m_parent;
    IDXGIDevice*   m_real0;
    IDXGIDevice1*  m_real1;
    IDXGIDevice2*  m_real2;
    IDXGIDevice3*  m_real3;
    LONG           m_refs;
};

} // namespace wiz3d
