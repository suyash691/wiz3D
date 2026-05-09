/* NvDirectMode - ID3D11Device proxy
 *
 * Stage 1b-i: passthrough wrapper for ID3D11Device. The only "intelligent"
 * method is GetImmediateContext which returns our wrapped context (set
 * during D3D11CreateDevice) so that COM identity is preserved between the
 * context returned via ppImmediateContext and the one a later
 * GetImmediateContext call retrieves.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <unordered_set>

namespace NvDirectMode
{

class Context11Proxy;
class DXGIDeviceProxy;

class Device11Proxy : public ID3D11Device
{
public:
    explicit Device11Proxy(ID3D11Device* real);
    virtual ~Device11Proxy();

    // Set by the dllmain D3D11CreateDevice path right after the immediate
    // context is wrapped, so GetImmediateContext can return that same proxy
    // (with AddRef) instead of producing a different one each call.
    void SetImmediateContextProxy(Context11Proxy* ctxProxy);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (r == 0) { if (m_real) { m_real->Release(); m_real = nullptr; } delete this; }
        return (ULONG)r;
    }

    // ID3D11Device
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D11_BUFFER_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Buffer** ppBuffer) override                                       { return m_real->CreateBuffer(pDesc, pInitialData, ppBuffer); }
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D11_TEXTURE1D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture1D** ppTexture1D) override                          { return m_real->CreateTexture1D(pDesc, pInitialData, ppTexture1D); }
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) override                          { return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D); }
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D11_TEXTURE3D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture3D** ppTexture3D) override                          { return m_real->CreateTexture3D(pDesc, pInitialData, ppTexture3D); }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Resource* pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D11ShaderResourceView** ppSRView) override                 { return m_real->CreateShaderResourceView(pResource, pDesc, ppSRView); }
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Resource* pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc, ID3D11UnorderedAccessView** ppUAView) override             { return m_real->CreateUnorderedAccessView(pResource, pDesc, ppUAView); }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource* pResource, const D3D11_RENDER_TARGET_VIEW_DESC* pDesc, ID3D11RenderTargetView** ppRTView) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Resource* pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc, ID3D11DepthStencilView** ppDepthStencilView) override            { return m_real->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView); }
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout** ppInputLayout) override { return m_real->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout); }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader) override        { return m_real->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader); }
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader) override  { return m_real->CreateGeometryShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppGeometryShader); }
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void* pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY* pSODeclaration, UINT NumEntries, const UINT* pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader) override { return m_real->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream, pClassLinkage, ppGeometryShader); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11PixelShader** ppPixelShader) override           { return m_real->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader); }
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11HullShader** ppHullShader) override              { return m_real->CreateHullShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppHullShader); }
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11DomainShader** ppDomainShader) override        { return m_real->CreateDomainShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppDomainShader); }
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage, ID3D11ComputeShader** ppComputeShader) override     { return m_real->CreateComputeShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppComputeShader); }
    HRESULT STDMETHODCALLTYPE CreateClassLinkage(ID3D11ClassLinkage** ppLinkage) override                                                                                                    { return m_real->CreateClassLinkage(ppLinkage); }
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D11_BLEND_DESC* pBlendStateDesc, ID3D11BlendState** ppBlendState) override                                                            { return m_real->CreateBlendState(pBlendStateDesc, ppBlendState); }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc, ID3D11DepthStencilState** ppDepthStencilState) override                             { return m_real->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState); }
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D11_RASTERIZER_DESC* pRasterizerDesc, ID3D11RasterizerState** ppRasterizerState) override                                        { return m_real->CreateRasterizerState(pRasterizerDesc, ppRasterizerState); }
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D11_SAMPLER_DESC* pSamplerDesc, ID3D11SamplerState** ppSamplerState) override                                                       { return m_real->CreateSamplerState(pSamplerDesc, ppSamplerState); }
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC* pQueryDesc, ID3D11Query** ppQuery) override                                                                                { return m_real->CreateQuery(pQueryDesc, ppQuery); }
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D11_QUERY_DESC* pPredicateDesc, ID3D11Predicate** ppPredicate) override                                                                { return m_real->CreatePredicate(pPredicateDesc, ppPredicate); }
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D11_COUNTER_DESC* pCounterDesc, ID3D11Counter** ppCounter) override                                                                      { return m_real->CreateCounter(pCounterDesc, ppCounter); }
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext) override                                                                     { return m_real->CreateDeferredContext(ContextFlags, ppDeferredContext); }
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void** ppResource) override                                                                     { return m_real->OpenSharedResource(hResource, ReturnedInterface, ppResource); }
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT Format, UINT* pFormatSupport) override                                                                                          { return m_real->CheckFormatSupport(Format, pFormatSupport); }
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT* pNumQualityLevels) override                                                          { return m_real->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels); }
    void    STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO* pCounterInfo) override                                                                                                    { m_real->CheckCounterInfo(pCounterInfo); }
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D11_COUNTER_DESC* pDesc, D3D11_COUNTER_TYPE* pType, UINT* pActiveCounters, LPSTR szName, UINT* pNameLength, LPSTR szUnits, UINT* pUnitsLength, LPSTR szDescription, UINT* pDescriptionLength) override { return m_real->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength); }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) override                                                    { return m_real->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override                                                                                            { return m_real->GetPrivateData(guid, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override                                                                                        { return m_real->SetPrivateData(guid, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override                                                                                          { return m_real->SetPrivateDataInterface(guid, pData); }
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel() override                                                                                                                           { return m_real->GetFeatureLevel(); }
    UINT    STDMETHODCALLTYPE GetCreationFlags() override                                                                                                                                    { return m_real->GetCreationFlags(); }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override                                                                                                                              { return m_real->GetDeviceRemovedReason(); }
    void    STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) override;
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags) override                                                                                                                     { return m_real->SetExceptionMode(RaiseFlags); }
    UINT    STDMETHODCALLTYPE GetExceptionMode() override                                                                                                                                    { return m_real->GetExceptionMode(); }

    // Accessor for 1b-iii / 1b-iv to plumb through state mutators.
    ID3D11Device* GetReal() const { return m_real; }

    // Logical (one-eye) backbuffer size, set by DXGI swap-chain creation /
    // ResizeBuffers. Used by 1b-iv's OMSetRenderTargets viewport routing.
    void SetLogicalBackBufferSize(UINT w, UINT h) { m_logicalWidth = w; m_logicalHeight = h; }
    UINT GetLogicalBackBufferWidth()  const       { return m_logicalWidth; }
    UINT GetLogicalBackBufferHeight() const       { return m_logicalHeight; }

    // 1b-iv back-buffer-RTV identity tracking. The flow is:
    //   SwapChainProxy::GetBuffer(0)             -> RegisterBackBufferTexture
    //   Device11Proxy::CreateRenderTargetView(BB) -> TrackBackBufferRTV
    //   Context11Proxy::OMSetRenderTargets(rtv)   -> IsBackBufferRTV check
    //
    // Stored as void* / raw IUnknown so we don't have to worry about which
    // interface (Texture2D / Resource / DXGISurface) the game requested.
    // Pointer-identity is stable for COM single-inheritance chains.
    void RegisterBackBufferTexture(void* pTextureLike);
    bool IsBackBufferResource(ID3D11Resource* p) const;
    void TrackBackBufferRTV(ID3D11RenderTargetView* rtv);
    bool IsBackBufferRTV(ID3D11RenderTargetView* rtv) const;

private:
    // Helper for QueryInterface(IDXGIDevice*) — returns a new ref on the
    // cached proxy (creating + caching it on first call). Returns nullptr
    // if the real device doesn't expose any IDXGIDevice (shouldn't happen
    // for a real D3D11 device).
    //
    // Lifetime: the cache holds one strong ref on the proxy until our
    // own destructor runs. That's what closes the resurrection race —
    // a thread can't AddRef the cached proxy "after" it was deleted,
    // because our cache ref keeps it alive as long as the parent device
    // is alive. When the parent device dies, ~Device11Proxy detaches
    // the parent pointer (so any outstanding DXGIDeviceProxy refs the
    // game still holds get E_NOINTERFACE on QI(ID3D11Device)) and then
    // Releases the cache ref.
    DXGIDeviceProxy* GetOrCreateDXGIDeviceProxyAddRef();

    ID3D11Device*    m_real;
    Context11Proxy*  m_ctxProxy;       // not owned — created in dllmain, freed on Release chain
    DXGIDeviceProxy* m_dxgiDeviceProxy;// strong ref (released in ~Device11Proxy)
    CRITICAL_SECTION m_dxgiCacheLock;
    LONG             m_refs;
    UINT             m_logicalWidth;
    UINT             m_logicalHeight;

    // 1b-iv state. We don't AddRef any of these — game manages lifetimes;
    // we just identity-track. A released-and-reallocated pointer would be
    // a stale-positive worst case (game gets per-eye routing on a non-BB
    // RTV, harmless — only the viewport changes, no crash).
    void*            m_pBackBufferResource;
    std::unordered_set<ID3D11RenderTargetView*> m_backBufferRTVs;
    CRITICAL_SECTION m_rtvSetLock;
};

} // namespace NvDirectMode
