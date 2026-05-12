/* NvDirectMode/d3d11 - NVIDIA 3D Vision magic-header SBS capture
 *
 * DX11 equivalent of NvDirectMode/d3d9 + d3d10 magic_header_capture. Same
 * protocol (NVSTEREO_IMAGE_SIGNATURE in last row of a 2W × H+1 surface),
 * just on top of D3D11 + DeviceContext.
 *
 * Reference: NVIDIA 3D Vision Programming Guide. Struct + signature constant
 * from Helifax's OGL-3DVision-Wrapper (MIT, see THIRD_PARTY_NOTICES.txt).
 */

#pragma once

#include <d3d11.h>

namespace NvDirectMode {
namespace MagicHeader {

constexpr unsigned int kSignature       = 0x4433564Eu;
constexpr unsigned int kFlagSwapEyes    = 0x00000001u;
constexpr unsigned int kFlagScaleToFit  = 0x00000002u;
constexpr unsigned int kFlagScaleToFit2 = 0x00000004u;

struct NvStereoImageHeader
{
    unsigned int dwSignature;
    unsigned int dwWidth;
    unsigned int dwHeight;
    unsigned int dwBPP;
    unsigned int dwFlags;
};

struct DetectResult
{
    bool  hasMagic;
    bool  swapEyes;
    UINT  eyeWidth;
    UINT  eyeHeight;
};

// Probe a D3D11 source resource (Texture2D) for the NV stereo magic. Needs
// both the device and a context (for CopyResource→staging + Map READ).
DetectResult DetectStereoMagic(ID3D11Device* pDevice,
                               ID3D11DeviceContext* pContext,
                               ID3D11Resource* pSrc);

// Split a stereo source texture into two per-eye destination textures via
// CopySubresourceRegion. Honours SIH_SWAP_EYES.
HRESULT SplitStereoTexture(ID3D11DeviceContext* pContext,
                           ID3D11Resource* pSrc,
                           UINT eyeWidth, UINT eyeHeight,
                           ID3D11Texture2D* pLeftDst,
                           ID3D11Texture2D* pRightDst,
                           bool swapEyes);

} // namespace MagicHeader
} // namespace NvDirectMode
