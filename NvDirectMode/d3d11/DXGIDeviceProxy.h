/* NvDirectMode - IDXGIDevice / IDXGIDevice1/2/3 proxy
 *
 * Stage 2: closes the COM-identity loop between Device11Proxy (the wrapper
 * the game holds via D3D11CreateDevice) and the IDXGIDevice the game gets
 * from `device->QueryInterface(IID_IDXGIDevice, ...)`.
 *
 * Without this wrapper, Device11Proxy::QI(IID_IDXGIDevice) forwards to the
 * real device, which returns a real IDXGIDevice whose own QI(ID3D11Device)
 * loops back to the *real* device — not to our Device11Proxy. Tomb Raider
 * 2013 (and other games whose engine layer does refcount math through both
 * the IDXGIDevice and the ID3D11Device pointers) ends up double-releasing
 * the real device when our proxy drops its own ref, crashing in the CRT
 * `free` path inside ucrtbase.
 *
 * With this wrapper:
 *   game -> dev->QI(IDXGIDevice)               -> DXGIDeviceProxy
 *           dxgiProxy->QI(ID3D11Device)        -> our Device11Proxy
 *           dxgiProxy->Release()               -> only releases dxgiProxy's
 *                                                 own QI'd ref on the real
 *                                                 IDXGIDevice
 *           dev->Release()                     -> Device11Proxy releases
 *                                                 the real ID3D11Device
 * Refcount on the real device stays balanced; game's mental model of "one
 * release per QI" round-trips correctly.
 *
 * Lifetime:
 *   Cached on the parent Device11Proxy (raw pointer, no AddRef on parent).
 *   Repeat QI calls return the same proxy (with AddRef) — important for
 *   COM identity equality checks. When the proxy's refcount hits zero it
 *   notifies the parent to clear the cache, releases its own QI'd refs on
 *   the real IDXGIDevice family, and self-deletes.
 *
 * We claim up to IDXGIDevice3 (Windows 8.1 era — covers TR2013 / Hard Reset
 * / FC2 / Lost Planet / etc.). IDXGIDevice4's two methods (OfferResources1
 * / ReclaimResources1) are forwarded via QI on the real device only when
 * a game explicitly QIs for it; we don't claim IDXGIDevice4 from this proxy
 * to keep the SDK header surface tight (dxgi1_5.h). If a future Direct Mode
 * game needs IDXGIDevice4-level identity preservation we can extend.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>

namespace NvDirectMode
{

class Device11Proxy;

class DXGIDeviceProxy : public IDXGIDevice3
{
public:
    // Constructor expects all version pointers we successfully QI'd on the
    // real device. r0 is required (always present); r1/r2/r3 are nullable.
    // Each non-null pointer holds one ref; Release walks them at destruction.
    DXGIDeviceProxy(Device11Proxy* parent,
                    IDXGIDevice*  r0,
                    IDXGIDevice1* r1,
                    IDXGIDevice2* r2,
                    IDXGIDevice3* r3);
    virtual ~DXGIDeviceProxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override   { return m_real0->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnk) override      { return m_real0->SetPrivateDataInterface(Name, pUnk); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override       { return m_real0->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override                          { return m_real0->GetParent(riid, ppParent); }

    // IDXGIDevice
    HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** ppAdapter) override                             { return m_real0->GetAdapter(ppAdapter); }
    HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC* pDesc, UINT NumSurfaces,
                                            DXGI_USAGE Usage, const DXGI_SHARED_RESOURCE* pSharedResource,
                                            IDXGISurface** ppSurface) override                          { return m_real0->CreateSurface(pDesc, NumSurfaces, Usage, pSharedResource, ppSurface); }
    HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown* const* ppResources,
                                                     DXGI_RESIDENCY* pResidencyStatus, UINT NumResources) override
                                                                                                        { return m_real0->QueryResourceResidency(ppResources, pResidencyStatus, NumResources); }
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT Priority) override                               { return m_real0->SetGPUThreadPriority(Priority); }
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* pPriority) override                             { return m_real0->GetGPUThreadPriority(pPriority); }

    // IDXGIDevice1
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override                          { return m_real1 ? m_real1->SetMaximumFrameLatency(MaxLatency) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override                        { return m_real1 ? m_real1->GetMaximumFrameLatency(pMaxLatency) : E_NOINTERFACE; }

    // IDXGIDevice2
    HRESULT STDMETHODCALLTYPE OfferResources(UINT NumResources, IDXGIResource* const* ppResources,
                                             DXGI_OFFER_RESOURCE_PRIORITY Priority) override            { return m_real2 ? m_real2->OfferResources(NumResources, ppResources, Priority) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE ReclaimResources(UINT NumResources, IDXGIResource* const* ppResources,
                                               BOOL* pDiscarded) override                               { return m_real2 ? m_real2->ReclaimResources(NumResources, ppResources, pDiscarded) : E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE EnqueueSetEvent(HANDLE hEvent) override                                   { return m_real2 ? m_real2->EnqueueSetEvent(hEvent) : E_NOINTERFACE; }

    // IDXGIDevice3
    void    STDMETHODCALLTYPE Trim() override                                                           { if (m_real3) m_real3->Trim(); }

    int  GetHighestVersion() const { return m_highestVer; }

    // Called by ~Device11Proxy. Severs the back-pointer so any outstanding
    // game-held refs on this proxy no longer route IID_ID3D11Device QIs
    // through a destructed parent (they return E_NOINTERFACE instead).
    void DetachParent() { m_parent = nullptr; }

private:
    Device11Proxy* m_parent;       // not AddRef'd; parent always outlives this proxy
    IDXGIDevice*   m_real0;
    IDXGIDevice1*  m_real1;
    IDXGIDevice2*  m_real2;
    IDXGIDevice3*  m_real3;
    int            m_highestVer;   // 0..3
    LONG           m_refs;
};

} // namespace NvDirectMode
