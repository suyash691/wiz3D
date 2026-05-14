/* NvDirectMode - ID3D11DeviceContext proxy
 *
 * Stage 1b-i: passthrough wrapper. Stage 1b-iv hooks OMSetRenderTargets +
 * OMSetRenderTargetsAndUnorderedAccessViews to redirect viewport per active
 * eye when a wrapped backbuffer RTV is bound at slot 0.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <functional>
#include <vector>

namespace wiz3d
{

// Stage 4b.4: RAII wrapper for COM pointers captured inside frame-recording
// lambdas. The lambda needs to keep the pointer alive across the frame even
// if the game releases its own ref, AND std::function requires the captured
// state to be copy-constructible (so we Release-on-destruct rather than
// move-only). Copy-ctor AddRefs, dtor Releases, so a std::vector<ComRefHolder>
// can be captured by value into a lambda and the ref count stays balanced.
struct ComRefHolder
{
    IUnknown* p;

    ComRefHolder() : p(nullptr) {}
    explicit ComRefHolder(IUnknown* x) : p(x) { if (p) p->AddRef(); }
    ComRefHolder(const ComRefHolder& o) : p(o.p) { if (p) p->AddRef(); }
    ComRefHolder(ComRefHolder&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComRefHolder& operator=(const ComRefHolder& o)
    {
        if (this != &o)
        {
            if (p) p->Release();
            p = o.p;
            if (p) p->AddRef();
        }
        return *this;
    }
    ComRefHolder& operator=(ComRefHolder&& o) noexcept
    {
        if (this != &o)
        {
            if (p) p->Release();
            p = o.p;
            o.p = nullptr;
        }
        return *this;
    }
    ~ComRefHolder() { if (p) p->Release(); }
};

class Device11Proxy;

class Context11Proxy : public ID3D11DeviceContext
{
public:
    Context11Proxy(ID3D11DeviceContext* real, Device11Proxy* parent);
    virtual ~Context11Proxy();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override;
    ULONG   STDMETHODCALLTYPE AddRef() override                                  { return InterlockedIncrement(&m_refs); }
    ULONG   STDMETHODCALLTYPE Release() override
    {
        LONG r = InterlockedDecrement(&m_refs);
        if (r == 0) { if (m_real) { m_real->Release(); m_real = nullptr; } delete this; }
        return (ULONG)r;
    }

    // ID3D11DeviceChild
    void    STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;  // returns wrapped parent
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override         { return m_real->GetPrivateData(guid, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override     { return m_real->SetPrivateData(guid, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override       { return m_real->SetPrivateDataInterface(guid, pData); }

    // ID3D11DeviceContext — input assembler / shader stages / rasteriser / output merger / draws
    void    STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    void    STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void    STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader* pPixelShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override;
    void    STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void    STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override;
    void    STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override;
    void    STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override;
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) override;
    void    STDMETHODCALLTYPE Unmap(ID3D11Resource* pResource, UINT Subresource) override;
    void    STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    void    STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout* pInputLayout) override;
    void    STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) override;
    void    STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override;
    void    STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override;
    void    STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override;
    void    STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    void    STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader* pShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override;
    void    STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override;
    void    STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void    STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE Begin(ID3D11Asynchronous* pAsync) override                                                                                                                              { m_real->Begin(pAsync); }
    void STDMETHODCALLTYPE End(ID3D11Asynchronous* pAsync) override                                                                                                                                { m_real->End(pAsync); }
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags) override                                                                          { return m_real->GetData(pAsync, pData, DataSize, GetDataFlags); }
    void    STDMETHODCALLTYPE SetPredication(ID3D11Predicate* pPredicate, BOOL PredicateValue) override;
    void    STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void    STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView) override;
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) override;
    void    STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState* pBlendState, const FLOAT BlendFactor[4], UINT SampleMask) override;
    void    STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState* pDepthStencilState, UINT StencilRef) override;
    void    STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D11Buffer* const* ppSOTargets, const UINT* pOffsets) override;
    void    STDMETHODCALLTYPE DrawAuto() override;
    void    STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void    STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void    STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override;
    void    STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void    STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState* pRasterizerState) override;
    void    STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) override;
    void    STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) override;
    // Intercepted out-of-line for the NV magic-header SBS capture path.
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource, const D3D11_BOX* pSrcBox) override;
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) override;
    void    STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;
    void    STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView* pSrcView) override;
    void    STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUnorderedAccessView, const UINT Values[4]) override                                                            { m_real->ClearUnorderedAccessViewUint(pUnorderedAccessView, Values); }
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUnorderedAccessView, const FLOAT Values[4]) override                                                          { m_real->ClearUnorderedAccessViewFloat(pUnorderedAccessView, Values); }
    void    STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override;
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView* pShaderResourceView) override                                                                                                    { m_real->GenerateMips(pShaderResourceView); }
    void STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource* pResource, FLOAT MinLOD) override                                                                                                     { m_real->SetResourceMinLOD(pResource, MinLOD); }
    FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource* pResource) override                                                                                                                  { return m_real->GetResourceMinLOD(pResource); }
    void    STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, ID3D11Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override;
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList* pCommandList, BOOL RestoreContextState) override                                                                                  { m_real->ExecuteCommandList(pCommandList, RestoreContextState); }
    void    STDMETHODCALLTYPE HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void    STDMETHODCALLTYPE HSSetShader(ID3D11HullShader* pHullShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override;
    void    STDMETHODCALLTYPE HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void    STDMETHODCALLTYPE HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    void    STDMETHODCALLTYPE DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void    STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader* pDomainShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override;
    void    STDMETHODCALLTYPE DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void    STDMETHODCALLTYPE DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    void    STDMETHODCALLTYPE CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView* const* ppShaderResourceViews) override;
    void    STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews, const UINT* pUAVInitialCounts) override;
    void    STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader* pComputeShader, ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) override;
    void    STDMETHODCALLTYPE CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers) override;
    void    STDMETHODCALLTYPE CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer* const* ppConstantBuffers) override;
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override                                                                        { m_real->VSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->PSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader** ppPixelShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override                                               { m_real->PSGetShader(ppPixelShader, ppClassInstances, pNumClassInstances); }
    void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override                                                                               { m_real->PSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader** ppVertexShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override                                             { m_real->VSGetShader(ppVertexShader, ppClassInstances, pNumClassInstances); }
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override                                                                        { m_real->PSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout** ppInputLayout) override                                                                                                            { m_real->IAGetInputLayout(ppInputLayout); }
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppVertexBuffers, UINT* pStrides, UINT* pOffsets) override                                            { m_real->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets); }
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer** pIndexBuffer, DXGI_FORMAT* Format, UINT* Offset) override                                                                               { m_real->IAGetIndexBuffer(pIndexBuffer, Format, Offset); }
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override                                                                        { m_real->GSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader** ppGeometryShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override                                         { m_real->GSGetShader(ppGeometryShader, ppClassInstances, pNumClassInstances); }
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* pTopology) override                                                                                                    { m_real->IAGetPrimitiveTopology(pTopology); }
    void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->VSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override                                                                               { m_real->VSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE GetPredication(ID3D11Predicate** ppPredicate, BOOL* pPredicateValue) override                                                                                           { m_real->GetPredication(ppPredicate, pPredicateValue); }
    void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->GSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override                                                                               { m_real->GSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView** ppRenderTargetViews, ID3D11DepthStencilView** ppDepthStencilView) override                                   { m_real->OMGetRenderTargets(NumViews, ppRenderTargetViews, ppDepthStencilView); }
    void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView** ppRenderTargetViews, ID3D11DepthStencilView** ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUnorderedAccessViews) override { m_real->OMGetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, ppDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews); }
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState** ppBlendState, FLOAT BlendFactor[4], UINT* pSampleMask) override                                                                      { m_real->OMGetBlendState(ppBlendState, BlendFactor, pSampleMask); }
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState** ppDepthStencilState, UINT* pStencilRef) override                                                                       { m_real->OMGetDepthStencilState(ppDepthStencilState, pStencilRef); }
    void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers, ID3D11Buffer** ppSOTargets) override                                                                                                      { m_real->SOGetTargets(NumBuffers, ppSOTargets); }
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState** ppRasterizerState) override                                                                                                          { m_real->RSGetState(ppRasterizerState); }
    void STDMETHODCALLTYPE RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) override                                                                                                { m_real->RSGetViewports(pNumViewports, pViewports); }
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) override                                                                                                         { m_real->RSGetScissorRects(pNumRects, pRects); }
    void STDMETHODCALLTYPE HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->HSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader** ppHullShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override                                                 { m_real->HSGetShader(ppHullShader, ppClassInstances, pNumClassInstances); }
    void STDMETHODCALLTYPE HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override                                                                               { m_real->HSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override                                                                        { m_real->HSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->DSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader** ppDomainShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override                                             { m_real->DSGetShader(ppDomainShader, ppClassInstances, pNumClassInstances); }
    void STDMETHODCALLTYPE DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override                                                                               { m_real->DSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override                                                                        { m_real->DSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView** ppShaderResourceViews) override                                                          { m_real->CSGetShaderResources(StartSlot, NumViews, ppShaderResourceViews); }
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView** ppUnorderedAccessViews) override                                                    { m_real->CSGetUnorderedAccessViews(StartSlot, NumUAVs, ppUnorderedAccessViews); }
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader** ppComputeShader, ID3D11ClassInstance** ppClassInstances, UINT* pNumClassInstances) override                                           { m_real->CSGetShader(ppComputeShader, ppClassInstances, pNumClassInstances); }
    void STDMETHODCALLTYPE CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState** ppSamplers) override                                                                               { m_real->CSGetSamplers(StartSlot, NumSamplers, ppSamplers); }
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer** ppConstantBuffers) override                                                                        { m_real->CSGetConstantBuffers(StartSlot, NumBuffers, ppConstantBuffers); }
    void STDMETHODCALLTYPE ClearState() override                                                                                                                                                   { m_real->ClearState(); }
    void STDMETHODCALLTYPE Flush() override                                                                                                                                                        { m_real->Flush(); }
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType() override                                                                                                                                 { return m_real->GetType(); }
    UINT STDMETHODCALLTYPE GetContextFlags() override                                                                                                                                              { return m_real->GetContextFlags(); }
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList** ppCommandList) override                                                                      { return m_real->FinishCommandList(RestoreDeferredContextState, ppCommandList); }

    // Accessors for 1b-iv routing logic
    ID3D11DeviceContext* GetReal()  const { return m_real; }
    Device11Proxy*       GetParent() const { return m_parent; }

    // Stage 3 helpers — exposed so the shared OMSet hook can mark whether
    // the BB-RTV is currently bound (RSSetViewports needs to know).
    void SetCurrentBBBound(bool b) { m_currentBBBound = b; }
    bool GetCurrentBBBound() const { return m_currentBBBound; }

    // Stage 4a: active-eye state. Default is left. Stage 4d (SBS composite
    // at swap-chain Present) flips this to right between L/R passes so that
    // OMSetRenderTargets picks the right-eye real RTV/DSV when binding a
    // stereo-doubled view. Left and right textures were allocated as a pair
    // by Texture2D11Proxy's constructor (Stage 3b); 4a just decides which
    // one gets bound at draw time.
    enum class Eye { Left = 0, Right = 1 };
    void SetActiveEye(Eye e) { m_activeEye = e; }
    Eye  GetActiveEye() const { return m_activeEye; }

    // Stage 4b.1: frame-recording INFRASTRUCTURE — the storage vector,
    // a clear hook, and a replay-with-eye method. iZ3D's StereoCommandBuffer
    // pattern ported to COM: each recorded entry is a std::function that
    // re-invokes the proxy-level call so the closure re-runs OUR intercept
    // logic — which picks the eye-appropriate real handle based on
    // m_activeEye at REPLAY time, not capture time. That's how setting
    // m_activeEye=Right before ReplayFrameCommands lets the same recorded
    // OMSet bind the right RTV.
    void ClearFrameCommands();
    void ReplayFrameCommands(Eye eye);

    // Stage 4b.4: gate for whether OMSet/state-setters actually push into
    // m_frameCommands. Set true the first time SwapChain11Proxy's Present
    // hook fires on our parent's swap chain (so we know the vector will be
    // flushed each frame). Stays false for games whose swap chain bypasses
    // us (e.g. D3D11CreateDevice + factory->CreateSwapChain), so we never
    // accumulate an unbounded recording. The 4b.8 factory hook will close
    // that gap; until then it's safer to skip recording than to leak.
    void SetPresentHookActive(bool active) { m_presentHookActive = active; }
    bool IsPresentHookActive() const { return m_presentHookActive; }

private:
    // Stage 4b.4: private "Do" versions of state-setting methods that the
    // public ID3D11DeviceContext overrides delegate to. The recorded
    // lambdas also call these Do* helpers directly, so replay re-runs the
    // eye-aware unwrap logic with whatever m_activeEye is set to at replay
    // time without triggering another record (no infinite recursion).
    void DoOMSetRenderTargets(UINT NumViews,
                              ID3D11RenderTargetView* const* ppRenderTargetViews,
                              ID3D11DepthStencilView* pDepthStencilView);
    void DoOMSetRenderTargetsAndUnorderedAccessViews(
        UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews,
        ID3D11DepthStencilView* pDepthStencilView,
        UINT UAVStartSlot, UINT NumUAVs,
        ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
        const UINT* pUAVInitialCounts);

    // Stage 4b.6: eye-aware Copy/Update/Resolve/Clear helpers. Same shape as
    // DoOMSetRenderTargets — pick the left- or right-eye real handle per
    // wrapped resource/view depending on m_activeEye. Recording closures
    // invoke these directly so replay re-runs the eye selection with the
    // active eye set by ReplayFrameCommands.
    void DoCopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource);
    void DoCopySubresourceRegion(
        ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
        UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource,
        const D3D11_BOX* pSrcBox);
    void DoUpdateSubresource(
        ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox,
        const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch);
    void DoResolveSubresource(
        ID3D11Resource* pDstResource, UINT DstSubresource,
        ID3D11Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format);
    void DoClearRenderTargetView(
        ID3D11RenderTargetView* pRenderTargetView, const FLOAT ColorRGBA[4]);
    void DoClearDepthStencilView(
        ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil);

    ID3D11DeviceContext* m_real;
    Device11Proxy*       m_parent;
    LONG                 m_refs;
    bool                 m_currentBBBound;       // last OMSet had BB-RTV at slot 0
    Eye                  m_activeEye;            // Stage 4a: which eye OMSet binds
    bool                 m_presentHookActive;    // Stage 4b.4 safety gate

    // Stage 4b.1: per-frame command record. Stage 4b.8 + 4d flush + replay
    // before each Present. Only populated when m_presentHookActive is true.
    std::vector<std::function<void()>> m_frameCommands;

    // Stage 4b.5: active-map tracking. D3D11's Map() returns a pointer the
    // game writes into; only at Unmap() is the data committed to GPU. We can
    // only capture the bytes once the writes are done — i.e. at Unmap, BEFORE
    // we forward Unmap to the real context. So Map stashes the mapped pointer
    // plus the resource's byte size, and Unmap memcpy's that many bytes into
    // a snapshot which the replay closure re-issues. Buffer-only for now;
    // texture map replay would need pitch/slice metadata (deferred to 4b.6+).
    struct ActiveMap
    {
        ID3D11Resource* resource;       // game's pointer (Map keeps it pinned)
        UINT            subresource;
        D3D11_MAP       mapType;
        void*           mappedData;
        UINT            byteWidth;
    };
    std::vector<ActiveMap> m_activeMaps;
};

} // namespace wiz3d
