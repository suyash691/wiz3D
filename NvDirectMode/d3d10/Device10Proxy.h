/* NvDirectMode - ID3D10Device proxy
 *
 * d3d10 has no Context split — ID3D10Device handles both resource creation
 * and state/draws. So this single class is the d3d10 equivalent of d3d11's
 * Device11Proxy + Context11Proxy combined.
 *
 * Stage 1b-i passthrough for ~90 methods. Stage 1b-iv hooks
 * CreateRenderTargetView (track RTVs derived from the back-buffer texture)
 * and OMSetRenderTargets (clamp viewport per active eye when a tracked
 * back-buffer RTV is bound at slot 0).
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <unordered_set>

namespace NvDirectMode
{

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
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers) override                                                                  { m_real->VSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews) override                                                    { m_real->PSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE PSSetShader(ID3D10PixelShader* pPixelShader) override                                                                                                                   { m_real->PSSetShader(pPixelShader); }
    void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers) override                                                                         { m_real->PSSetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE VSSetShader(ID3D10VertexShader* pVertexShader) override                                                                                                                 { m_real->VSSetShader(pVertexShader); }
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override                                                                                  { m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation); }
    void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override                                                                                                               { m_real->Draw(VertexCount, StartVertexLocation); }
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers) override                                                                  { m_real->PSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE IASetInputLayout(ID3D10InputLayout* pInputLayout) override                                                                                                              { m_real->IASetInputLayout(pInputLayout); }
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) override                          { m_real->IASetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets); }
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D10Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override                                                                                  { m_real->IASetIndexBuffer(pIndexBuffer, Format, Offset); }
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override              { m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation); }
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override                                           { m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation); }
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D10Buffer* const* ppConstantBuffers) override                                                                  { m_real->GSSetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE GSSetShader(ID3D10GeometryShader* pShader) override                                                                                                                     { m_real->GSSetShader(pShader); }
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY Topology) override                                                                                                      { m_real->IASetPrimitiveTopology(Topology); }
    void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews) override                                                    { m_real->VSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers) override                                                                         { m_real->VSSetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE SetPredication(ID3D10Predicate* pPredicate, BOOL PredicateValue) override                                                                                               { m_real->SetPredication(pPredicate, PredicateValue); }
    void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D10ShaderResourceView* const* ppShaderResourceViews) override                                                    { m_real->GSSetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D10SamplerState* const* ppSamplers) override                                                                         { m_real->GSSetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D10RenderTargetView* const* ppRenderTargetViews, ID3D10DepthStencilView* pDepthStencilView) override;  // 1b-iv
    void STDMETHODCALLTYPE OMSetBlendState(ID3D10BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) override                                                                    { m_real->OMSetBlendState(pBlendState, BlendFactor, SampleMask); }
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D10DepthStencilState* pDepthStencilState, UINT StencilRef) override                                                                           { m_real->OMSetDepthStencilState(pDepthStencilState, StencilRef); }
    void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D10Buffer* const* ppSOTargets, const UINT* pOffsets) override                                                                          { m_real->SOSetTargets(NumBuffers, ppSOTargets, pOffsets); }
    void STDMETHODCALLTYPE DrawAuto() override                                                                                                                                                     { m_real->DrawAuto(); }
    void STDMETHODCALLTYPE RSSetState(ID3D10RasterizerState* pRasterizerState) override                                                                                                            { m_real->RSSetState(pRasterizerState); }
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D10_VIEWPORT* pViewports) override                                                                                            { m_real->RSSetViewports(NumViewports, pViewports); }
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D10_RECT* pRects) override                                                                                                     { m_real->RSSetScissorRects(NumRects, pRects); }
    // CopyResource/CopySubresourceRegion are intercepted (out-of-line in
    // Device10Proxy.cpp) so we can detect the NV magic-header SBS pattern
    // and route per-eye to the swap-chain's eye textures.
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D10Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D10Resource* pSrcResource, UINT SrcSubresource, const D3D10_BOX* pSrcBox) override;
    void STDMETHODCALLTYPE CopyResource(ID3D10Resource* pDstResource, ID3D10Resource* pSrcResource) override;
    void STDMETHODCALLTYPE UpdateSubresource(ID3D10Resource* pDstResource, UINT DstSubresource, const D3D10_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override     { m_real->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch); }
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D10RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) override                                                                     { m_real->ClearRenderTargetView(pRenderTargetView, ColorRGBA); }
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D10DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override                                                  { m_real->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil); }
    void STDMETHODCALLTYPE GenerateMips(ID3D10ShaderResourceView* pShaderResourceView) override                                                                                                    { m_real->GenerateMips(pShaderResourceView); }
    void STDMETHODCALLTYPE ResolveSubresource(ID3D10Resource* pDstResource, UINT DstSubresource, ID3D10Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override                   { m_real->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format); }
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
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D10_BUFFER_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Buffer** ppBuffer) override                                            { return m_real->CreateBuffer(pDesc, pInitialData, ppBuffer); }
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D10_TEXTURE1D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture1D** ppTexture1D) override                                { return m_real->CreateTexture1D(pDesc, pInitialData, ppTexture1D); }
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D10_TEXTURE2D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture2D** ppTexture2D) override                                { return m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D); }
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D10_TEXTURE3D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture3D** ppTexture3D) override                                { return m_real->CreateTexture3D(pDesc, pInitialData, ppTexture3D); }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D10Resource* pResource, const D3D10_SHADER_RESOURCE_VIEW_DESC* pDesc, ID3D10ShaderResourceView** ppSRView) override                       { return m_real->CreateShaderResourceView(pResource, pDesc, ppSRView); }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D10Resource* pResource, const D3D10_RENDER_TARGET_VIEW_DESC* pDesc, ID3D10RenderTargetView** ppRTView) override;  // 1b-iv
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D10Resource* pResource, const D3D10_DEPTH_STENCIL_VIEW_DESC* pDesc, ID3D10DepthStencilView** ppDepthStencilView) override                  { return m_real->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView); }
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

    // Accessors / state-mutators used by SwapChainProxy and dllmain.
    ID3D10Device* GetReal() const { return m_real; }

    void SetLogicalBackBufferSize(UINT w, UINT h) { m_logicalWidth = w; m_logicalHeight = h; }
    UINT GetLogicalBackBufferWidth()  const       { return m_logicalWidth; }
    UINT GetLogicalBackBufferHeight() const       { return m_logicalHeight; }

    void RegisterBackBufferTexture(void* pTextureLike);
    bool IsBackBufferResource(ID3D10Resource* p) const;
    void TrackBackBufferRTV(ID3D10RenderTargetView* rtv);
    bool IsBackBufferRTV(ID3D10RenderTargetView* rtv) const;

private:
    ID3D10Device*    m_real;
    LONG             m_refs;
    UINT             m_logicalWidth;
    UINT             m_logicalHeight;
    void*            m_pBackBufferResource;
    std::unordered_set<ID3D10RenderTargetView*> m_backBufferRTVs;
    CRITICAL_SECTION m_rtvSetLock;
};

} // namespace NvDirectMode
