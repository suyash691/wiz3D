/* NvDirectMode/d3d11 - magic-header SBS capture impl. */

#include "magic_header_capture.h"
#include "log.h"

namespace NvDirectMode {
namespace MagicHeader {

namespace {

bool QueryTexture2D(ID3D11Resource* res, D3D11_TEXTURE2D_DESC& outDesc,
                    ID3D11Texture2D*& outTex)
{
    outTex = nullptr;
    if (!res) return false;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return false;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&outTex)) || !outTex)
        return false;
    outTex->GetDesc(&outDesc);
    return true;
}

bool ReadMagicFromStaging(ID3D11DeviceContext* ctx, ID3D11Texture2D* stage,
                          UINT srcHeight, unsigned int& out_flags)
{
    D3D11_MAPPED_SUBRESOURCE map = {};
    HRESULT hr = ctx->Map(stage, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr) || !map.pData) return false;

    const NvStereoImageHeader* hdr = reinterpret_cast<const NvStereoImageHeader*>(
        static_cast<const unsigned char*>(map.pData) + map.RowPitch * (srcHeight - 1));
    bool magic = (hdr->dwSignature == kSignature);
    if (magic) out_flags = hdr->dwFlags;
    ctx->Unmap(stage, 0);
    return magic;
}

} // anonymous namespace

DetectResult DetectStereoMagic(ID3D11Device* pDevice,
                               ID3D11DeviceContext* pContext,
                               ID3D11Resource* pSrc)
{
    DetectResult r = { false, false, 0, 0 };
    if (!pDevice || !pContext || !pSrc) return r;

    D3D11_TEXTURE2D_DESC desc = {};
    ID3D11Texture2D* srcTex = nullptr;
    if (!QueryTexture2D(pSrc, desc, srcTex)) return r;

    if (desc.Width < 4 || (desc.Width & 1) || desc.Height < 2)
    {
        srcTex->Release();
        return r;
    }

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage              = D3D11_USAGE_STAGING;
    sd.BindFlags          = 0;
    sd.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags          = 0;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;

    ID3D11Texture2D* stage = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&sd, nullptr, &stage);
    if (FAILED(hr) || !stage)
    {
        srcTex->Release();
        return r;
    }

    pContext->CopyResource(stage, srcTex);
    unsigned int flags = 0;
    bool found = ReadMagicFromStaging(pContext, stage, desc.Height, flags);
    stage->Release();
    srcTex->Release();
    if (!found) return r;

    r.hasMagic  = true;
    r.swapEyes  = (flags & kFlagSwapEyes) != 0;
    r.eyeWidth  = desc.Width / 2;
    r.eyeHeight = desc.Height - 1;
    LOG_VERBOSE("  d3d11 MagicHeader: detected NV3D on tex=%p, %ux%u eye, flags=0x%08X (swapEyes=%d)\n",
                (void*)pSrc, r.eyeWidth, r.eyeHeight, flags, r.swapEyes);
    return r;
}

HRESULT SplitStereoTexture(ID3D11DeviceContext* pContext,
                           ID3D11Resource* pSrc,
                           UINT eyeWidth, UINT eyeHeight,
                           ID3D11Texture2D* pLeftDst,
                           ID3D11Texture2D* pRightDst,
                           bool swapEyes)
{
    if (!pContext || !pSrc || !pLeftDst || !pRightDst) return E_POINTER;

    D3D11_BOX leftBox  = { 0,         0, 0, eyeWidth,     eyeHeight, 1 };
    D3D11_BOX rightBox = { eyeWidth,  0, 0, eyeWidth * 2, eyeHeight, 1 };
    ID3D11Texture2D* leftOnto  = swapEyes ? pRightDst : pLeftDst;
    ID3D11Texture2D* rightOnto = swapEyes ? pLeftDst  : pRightDst;

    pContext->CopySubresourceRegion(leftOnto,  0, 0, 0, 0, pSrc, 0, &leftBox);
    pContext->CopySubresourceRegion(rightOnto, 0, 0, 0, 0, pSrc, 0, &rightBox);
    return S_OK;
}

} // namespace MagicHeader
} // namespace NvDirectMode
