/* NvDirectMode/d3d10 - NVIDIA 3D Vision magic-header SBS capture
 *
 * DX10 equivalent of NvDirectMode/d3d9/magic_header_capture.h. See that file
 * for the full design notes. The protocol is identical (NVSTEREO_IMAGE_SIGNATURE
 * in the last row of a 2W × H+1 surface) — only the API surface differs.
 *
 * Reference: NVIDIA 3D Vision Programming Guide. Struct + signature constant
 * + understanding of the protocol come from Helifax's OGL-3DVision-Wrapper
 * (MIT, see THIRD_PARTY_NOTICES.txt).
 */

#pragma once

#include <d3d10.h>

namespace NvDirectMode {
namespace MagicHeader {

constexpr unsigned int kSignature       = 0x4433564Eu;   // "NV3D"
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

// Inspect a D3D10 source resource for the NV stereo magic in its last row.
// Caller must pass an ID3D10Resource (typically a Texture2D). If the resource
// isn't readable directly (no CPU access flag), a one-shot staging texture is
// created and the data copied through it.
DetectResult DetectStereoMagic(ID3D10Device* pDevice, ID3D10Resource* pSrc);

// Split a stereo source texture into per-eye destination textures via
// CopySubresourceRegion. Each eye is sourced as a (eyeWidth × eyeHeight) box.
// Respects SIH_SWAP_EYES.
HRESULT SplitStereoTexture(ID3D10Device* pDevice,
                           ID3D10Resource* pSrc,
                           UINT eyeWidth, UINT eyeHeight,
                           ID3D10Texture2D* pLeftDst,
                           ID3D10Texture2D* pRightDst,
                           bool swapEyes);

} // namespace MagicHeader
} // namespace NvDirectMode
