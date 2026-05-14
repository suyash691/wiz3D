/* wiz3D - ID3D10Device proxy (Option B for DX10)
 *
 * D3D10 has no separate device/context split — ID3D10Device handles both
 * resource creation and state/draws on a single interface. So this single
 * class is the d3d10 equivalent of d3d11's Device11Proxy + Context11Proxy
 * combined.
 *
 * Stage 1 of the DX10 port: passthrough wrap so games on FC2 / JC2 / De Blob
 * land on the Option B path instead of the legacy DDI hooks (which break on
 * Win11 D3D11.10 layouts). Future stages will add per-eye routing, replay
 * sweep, SBS composite, etc. — mirroring what the DX11 side already has.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <functional>
#include <vector>

namespace wiz3d
{

// RAII COM-pointer holder for closure captures. Same shape as the DX11
// Context11Proxy's ComRefHolder — keeps a reference alive across the
// frame even if the game releases its own.
struct ComRefHolder10
{
    IUnknown* p;
    ComRefHolder10() : p(nullptr) {}
    explicit ComRefHolder10(IUnknown* x) : p(x) { if (p) p->AddRef(); }
    ComRefHolder10(const ComRefHolder10& o) : p(o.p) { if (p) p->AddRef(); }
    ComRefHolder10(ComRefHolder10&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComRefHolder10& operator=(const ComRefHolder10& o)
    {
        if (this != &o) { if (p) p->Release(); p = o.p; if (p) p->AddRef(); }
        return *this;
    }
    ComRefHolder10& operator=(ComRefHolder10&& o) noexcept
    {
        if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; }
        return *this;
    }
    ~ComRefHolder10() { if (p) p->Release(); }
};

class Device10Proxy : public ID3D10Device
{
public:
    explicit Device10Proxy(ID3D10Device* real);
    virtual ~Device10Proxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (r == 0) { if (m_real) { m_real->Release(); m_real = nullptr; } delete this; }
        return (ULONG)r;
    }

    // ID3D10Device — input assembler / shader stages / rasteriser / OM / draws
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE PSSetShader(ID3D10PixelShader* pPixelShader) override;
    void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE VSSetShader(ID3D10VertexShader* pVertexShader) override;
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override;
    void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override;
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE IASetInputLayout(ID3D10InputLayout* pInputLayout) override;
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) override;
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D10Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE GSSetShader(ID3D10GeometryShader* pShader) override;
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY Topology) override;
    void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE SetPredication(ID3D10Predicate* pPredicate, BOOL PredicateValue) override                                                                                               { m_real->SetPredication(pPredicate, PredicateValue); }
    void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews) override;
    void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D10RenderTargetView* const* ppRenderTargetViews, ID3D10DepthStencilView* pDepthStencilView) override;
    void STDMETHODCALLTYPE OMSetBlendState(ID3D10BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) override;
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D10DepthStencilState* pDepthStencilState, UINT StencilRef) override;
    void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D10Buffer* const* ppSOTargets, const UINT* pOffsets) override;
    void STDMETHODCALLTYPE DrawAuto() override;
    void STDMETHODCALLTYPE RSSetState(ID3D10RasterizerState* pRasterizerState) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D10_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D10_RECT* pRects) override;
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D10Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D10Resource* pSrcResource, UINT SrcSubresource, const D3D10_BOX* pSrcBox) override;
    void STDMETHODCALLTYPE CopyResource(ID3D10Resource* pDstResource, ID3D10Resource* pSrcResource) override;
    void STDMETHODCALLTYPE UpdateSubresource(ID3D10Resource* pDstResource, UINT DstSubresource, const D3D10_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D10RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D10DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override;
    void STDMETHODCALLTYPE GenerateMips(ID3D10ShaderResourceView* pShaderResourceView) override                                                                                                    { m_real->GenerateMips(pShaderResourceView); }
    void STDMETHODCALLTYPE ResolveSubresource(ID3D10Resource* pDstResource, UINT DstSubresource, ID3D10Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override;
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer** ppConstantBuffers) override                                                                        { m_real->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE PSGetShader(ID3D10PixelShader** ppPixelShader) override                                                                                                                 { m_real->PSGetShader(ppPixelShader); }
    void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState** ppSamplers) override                                                                               { m_real->PSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE VSGetShader(ID3D10VertexShader** ppVertexShader) override                                                                                                               { m_real->VSGetShader(ppVertexShader); }
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer** ppConstantBuffers) override                                                                        { m_real->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D10InputLayout** ppInputLayout) override                                                                                                            { m_real->IAGetInputLayout(ppInputLayout); }
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer** ppVertexBuffers, UINT* pStrides, UINT* pOffsets) override                                            { m_real->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets); }
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D10Buffer** pIndexBuffer, DXGI_FORMAT* Format, UINT* Offset) override                                                                               { m_real->IAGetIndexBuffer(pIndexBuffer, Format, Offset); }
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer** ppConstantBuffers) override                                                                        { m_real->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE GSGetShader(ID3D10GeometryShader** ppGeometryShader) override                                                                                                           { m_real->GSGetShader(ppGeometryShader); }
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY* pTopology) override                                                                                                    { m_real->IAGetPrimitiveTopology(pTopology); }
    void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState** ppSamplers) override                                                                               { m_real->VSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE GetPredication(ID3D10Predicate** ppPredicate, BOOL* pPredicateValue) override                                                                                           { m_real->GetPredication(ppPredicate, pPredicateValue); }
    void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState** ppSamplers) override                                                                               { m_real->GSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews, ID3D10RenderTargetView** ppRenderTargetViews, ID3D10DepthStencilView** ppDepthStencilView) override                                   { m_real->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView); }
    void STDMETHODCALLTYPE OMGetBlendState(ID3D10BlendState** ppBlendState, FLOAT BlendFactor[4], UINT* pSampleMask) override                                                                      { m_real->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask); }
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D10DepthStencilState** ppDepthStencilState, UINT* pStencilRef) override                                                                       { m_real->OMGetDepthStencilState(ppDepthStencilState, pStencilRef); }
    void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers, ID3D10Buffer** ppSOTargets, UINT* pOffsets) override                                                                                      { m_real->SOGetTargets(NumBuffers, ppSOTargets, pOffsets); }
    void STDMETHODCALLTYPE RSGetState(ID3D10RasterizerState** ppRasterizerState) override                                                                                                          { m_real->RSGetState(ppRasterizerState); }
    void STDMETHODCALLTYPE RSGetViewports(UINT* NumViewports, D3D10_VIEWPORT* pViewports) override                                                                                                 { m_real->RSGetViewports(NumViewports, pViewports); }
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* NumRects, D3D10_RECT* pRects) override                                                                                                          { m_real->RSGetScissorRects(NumRects, pRects); }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override                                                                                                                                    { return m_real->GetDeviceRemovedReason(); }
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags) override                                                                                                                           { return m_real->SetExceptionMode(RaiseFlags); }
    UINT STDMETHODCALLTYPE GetExceptionMode() override                                                                                                                                             { return m_real->GetExceptionMode(); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override                                                                                                  { return m_real->GetPrivateData(guid, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override                                                                                              { return m_real->SetPrivateData(guid, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override                                                                                                { return m_real->SetPrivateDataInterface(guid, pData); }
    void STDMETHODCALLTYPE ClearState() override                                                                                                                                                   { m_real->ClearState(); }
    void STDMETHODCALLTYPE Flush() override                                                                                                                                                        { m_real->Flush(); }
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D10_BUFFER_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Buffer** ppBuffer) override;
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D10_TEXTURE1D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture1D** ppTexture1D) override                                { return m_real->CreateTexture1D(pDesc, pInitialData, ppTexture1D); }
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D10_TEXTURE2D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture2D** ppTexture2D) override;
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D10_TEXTURE3D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture3D** ppTexture3D) override                                { return m_real->CreateTexture3D(pDesc, pInitialData, ppTexture3D); }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D10Resource* pResource, const D3D10_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D10ShaderResourceView** ppSRView) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D10Resource* pResource, const D3D10_RENDER_TARGET_VIEW_DESC* pDesc, ID3D10RenderTargetView** ppRTView) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D10Resource* pResource, const D3D10_DEPTH_STENCIL_VIEW_DESC* pDesc, ID3D10DepthStencilView** ppDepthStencilView) override;
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D10_INPUT_ELEMENT_DESC* pInputElementDescs, UINT NumElements, const void* pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D10InputLayout** ppInputLayout) override { return m_real->CreateInputLayout(pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout); }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D10VertexShader** ppVertexShader) override                                                  { return m_real->CreateVertexShader(pShaderBytecode, BytecodeLength, ppVertexShader); }
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D10GeometryShader** ppGeometryShader) override                                            { return m_real->CreateGeometryShader(pShaderBytecode, BytecodeLength, ppGeometryShader); }
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void* pShaderBytecode, SIZE_T BytecodeLength, const D3D10_SO_DECLARATION_ENTRY* pSODeclaration, UINT NumEntries, UINT OutputStreamStride, ID3D10GeometryShader** ppGeometryShader) override { return m_real->CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, pSODeclaration, NumEntries, OutputStreamStride, ppGeometryShader); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength, ID3D10PixelShader** ppPixelShader) override                                                     { return m_real->CreatePixelShader(pShaderBytecode, BytecodeLength, ppPixelShader); }
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D10_BLEND_DESC* pBlendStateDesc, ID3D10BlendState** ppBlendState) override                                                                   { return m_real->CreateBlendState(pBlendStateDesc, ppBlendState); }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D10_DEPTH_STENCIL_DESC* pDepthStencilDesc, ID3D10DepthStencilState** ppDepthStencilState) override                                    { return m_real->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState); }
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D10_RASTERIZER_DESC* pRasterizerDesc, ID3D10RasterizerState** ppRasterizerState) override                                               { return m_real->CreateRasterizerState(pRasterizerDesc, ppRasterizerState); }
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D10_SAMPLER_DESC* pSamplerDesc, ID3D10SamplerState** ppSamplerState) override                                                              { return m_real->CreateSamplerState(pSamplerDesc, ppSamplerState); }
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D10_QUERY_DESC* pQueryDesc, ID3D10Query** ppQuery) override                                                                                       { return m_real->CreateQuery(pQueryDesc, ppQuery); }
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D10_QUERY_DESC* pPredicateDesc, ID3D10Predicate** ppPredicate) override                                                                       { return m_real->CreatePredicate(pPredicateDesc, ppPredicate); }
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D10_COUNTER_DESC* pCounterDesc, ID3D10Counter** ppCounter) override                                                                             { return m_real->CreateCounter(pCounterDesc, ppCounter); }
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT Format, UINT* pFormatSupport) override                                                                                                { return m_real->CheckFormatSupport(Format, pFormatSupport); }
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT* pNumQualityLevels) override                                                                { return m_real->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels); }
    void    STDMETHODCALLTYPE CheckCounterInfo(D3D10_COUNTER_INFO* pCounterInfo) override                                                                                                          { m_real->CheckCounterInfo(pCounterInfo); }
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D10_COUNTER_DESC* pDesc, D3D10_COUNTER_TYPE* pType, UINT* pActiveCounters, LPSTR szName, UINT* pNameLength, LPSTR szUnits, UINT* pUnitsLength, LPSTR szDescription, UINT* pDescriptionLength) override { return m_real->CheckCounter(pDesc, pType, pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription, pDescriptionLength); }
    UINT STDMETHODCALLTYPE GetCreationFlags() override                                                                                                                                             { return m_real->GetCreationFlags(); }
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void** ppResource) override                                                                            { return m_real->OpenSharedResource(hResource, ReturnedInterface, ppResource); }
    void STDMETHODCALLTYPE SetTextFilterSize(UINT Width, UINT Height) override                                                                                                                     { m_real->SetTextFilterSize(Width, Height); }
    void STDMETHODCALLTYPE GetTextFilterSize(UINT* pWidth, UINT* pHeight) override                                                                                                                 { m_real->GetTextFilterSize(pWidth, pHeight); }

    ID3D10Device* GetReal() const { return m_real; }

    // DX10 Stage 4a equivalent: active-eye state controls which real
    // RTV/DSV/RT the Do* helpers bind.
    enum class Eye { Left = 0, Right = 1 };
    void SetActiveEye(Eye e) { m_activeEye = e; }
    Eye  GetActiveEye() const { return m_activeEye; }

    void SetLogicalBackBufferSize(UINT w, UINT h) { m_logicalWidth = w; m_logicalHeight = h; }
    UINT GetLogicalBackBufferWidth()  const       { return m_logicalWidth; }
    UINT GetLogicalBackBufferHeight() const       { return m_logicalHeight; }

    // DX10 Stage 4b: replay infrastructure mirroring Context11Proxy. The
    // SwapChain10Proxy fires the Pre-Present hook which calls
    // ReplayFrameCommands(Right) on the device, then Post-Present clears
    // and arms the recorder for the next frame.
    void ClearFrameCommands();
    void ReplayFrameCommands(Eye eye);
    void SetPresentHookActive(bool active) { m_presentHookActive = active; }
    bool IsPresentHookActive() const       { return m_presentHookActive; }
    // 4c10: Buffer10Proxy::Unmap pushes Map+memcpy+eye-shift+Unmap
    // replay closures through this entry point so the per-eye CB math
    // applies on the right-eye replay sweep.
    void PushFrameCommand(std::function<void()> fn) { m_frameCommands.emplace_back(std::move(fn)); }
    Eye  ActiveEye()      const { return m_activeEye; }

private:
    ID3D10Device* m_real;
    LONG          m_refs;
    Eye           m_activeEye;
    UINT          m_logicalWidth;
    UINT          m_logicalHeight;
    bool          m_presentHookActive;
    std::vector<std::function<void()>> m_frameCommands;
};

} // namespace wiz3d
