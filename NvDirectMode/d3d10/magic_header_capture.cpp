/* NvDirectMode/d3d10 - magic-header SBS capture impl. */

#include "magic_header_capture.h"
#include "log.h"

namespace NvDirectMode {
namespace MagicHeader {

namespace {

// Returns true if the resource is a Texture2D and writes its desc + the
// underlying interface (Texture2D-typed) to out_tex (AddRef'd).
bool QueryTexture2D(ID3D10Resource* res, D3D10_TEXTURE2D_DESC& outDesc,
                    ID3D10Texture2D*& outTex)
{
    outTex = nullptr;
    if (!res) return false;
    D3D10_RESOURCE_DIMENSION dim = D3D10_RESOURCE_DIMENSION_UNKNOWN;
    res->GetType(&dim);
    if (dim != D3D10_RESOURCE_DIMENSION_TEXTURE2D) return false;
    if (FAILED(res->QueryInterface(__uuidof(ID3D10Texture2D), (void**)&outTex)) || !outTex)
        return false;
    outTex->GetDesc(&outDesc);
    return true;
}

// Read magic from the last row of a staging texture (with CPU read access).
bool ReadMagicFromStaging(ID3D10Texture2D* pStaging, UINT srcHeight,
                          unsigned int& out_flags)
{
    D3D10_MAPPED_TEXTURE2D map = {};
    HRESULT hr = pStaging->Map(0, D3D10_MAP_READ, 0, &map);
    if (FAILED(hr) || !map.pData) return false;

    const NvStereoImageHeader* hdr = reinterpret_cast<const NvStereoImageHeader*>(
        static_cast<const unsigned char*>(map.pData) + map.RowPitch * (srcHeight - 1));
    bool magic = (hdr->dwSignature == kSignature);
    if (magic) out_flags = hdr->dwFlags;
    pStaging->Unmap(0);
    return magic;
}

} // anonymous namespace

DetectResult DetectStereoMagic(ID3D10Device* pDevice, ID3D10Resource* pSrc)
{
    DetectResult r = { false, false, 0, 0 };
    if (!pDevice || !pSrc) return r;

    D3D10_TEXTURE2D_DESC desc = {};
    ID3D10Texture2D* srcTex = nullptr;
    if (!QueryTexture2D(pSrc, desc, srcTex)) return r;

    // Dimension fingerprint: 2W × H+1 with W >= 2.
    if (desc.Width < 4 || (desc.Width & 1) || desc.Height < 2)
    {
        srcTex->Release();
        return r;
    }

    // DX10 textures don't support direct CPU read unless created with
    // D3D10_CPU_ACCESS_READ + D3D10_USAGE_STAGING. Helifax's pipeline uses
    // D3D10_USAGE_DEFAULT shared with D3D9 via interop, so we always stage.
    D3D10_TEXTURE2D_DESC sd = desc;
    sd.Usage            = D3D10_USAGE_STAGING;
    sd.BindFlags        = 0;
    sd.CPUAccessFlags   = D3D10_CPU_ACCESS_READ;
    sd.MiscFlags        = 0;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    ID3D10Texture2D* stage = nullptr;
    HRESULT hr = pDevice->CreateTexture2D(&sd, nullptr, &stage);
    if (FAILED(hr) || !stage)
    {
        srcTex->Release();
        return r;
    }

    pDevice->CopyResource(stage, srcTex);
    unsigned int flags = 0;
    bool found = ReadMagicFromStaging(stage, desc.Height, flags);
    stage->Release();
    srcTex->Release();
    if (!found) return r;

    r.hasMagic  = true;
    r.swapEyes  = (flags & kFlagSwapEyes) != 0;
    r.eyeWidth  = desc.Width / 2;
    r.eyeHeight = desc.Height - 1;
    LOG_VERBOSE("  d3d10 MagicHeader: detected NV3D on tex=%p, %ux%u eye, flags=0x%08X (swapEyes=%d)\n",
                (void*)pSrc, r.eyeWidth, r.eyeHeight, flags, r.swapEyes);
    return r;
}

HRESULT SplitStereoTexture(ID3D10Device* pDevice,
                           ID3D10Resource* pSrc,
                           UINT eyeWidth, UINT eyeHeight,
                           ID3D10Texture2D* pLeftDst,
                           ID3D10Texture2D* pRightDst,
                           bool swapEyes)
{
    if (!pDevice || !pSrc || !pLeftDst || !pRightDst) return E_POINTER;

    // Source boxes (DX10 uses [front, back) for Z; we pass 0..1 for 2D).
    D3D10_BOX leftBox  = { 0,          0, 0, eyeWidth,     eyeHeight, 1 };
    D3D10_BOX rightBox = { eyeWidth,   0, 0, eyeWidth * 2, eyeHeight, 1 };

    ID3D10Texture2D* leftOnto  = swapEyes ? pRightDst : pLeftDst;
    ID3D10Texture2D* rightOnto = swapEyes ? pLeftDst  : pRightDst;

    pDevice->CopySubresourceRegion(leftOnto,  0, 0, 0, 0, pSrc, 0, &leftBox);
    pDevice->CopySubresourceRegion(rightOnto, 0, 0, 0, 0, pSrc, 0, &rightBox);
    return S_OK;
}

} // namespace MagicHeader
} // namespace NvDirectMode
