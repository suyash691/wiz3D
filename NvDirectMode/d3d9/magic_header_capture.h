/* NvDirectMode/d3d9 - NVIDIA 3D Vision magic-header SBS capture
 *
 * Detects surfaces tagged with NVSTEREO_IMAGE_SIGNATURE (the documented
 * "stereo blit defaults" pattern from NVIDIA's 3D Vision SDK) and splits
 * them into per-eye surfaces for downstream NvDirectMode output methods.
 *
 * Reference: NVIDIA 3D Vision Programming Guide / NV_Stereo_Image_Header.
 * Header struct + signature constant + understanding of the protocol come
 * from Helifax's OGL-3DVision-Wrapper (MIT, see THIRD_PARTY_NOTICES.txt).
 *
 * The pattern: an application produces a wide SBS surface of size
 *   width * 2  ×  (height + 1)
 * where the extra last row contains a 20-byte NV stereo header. The driver
 * inspects that row, sees the magic, and presents the surface in stereo.
 *
 * Games / wrappers known to use this path:
 *   - Helifax's OGL-3DVision-Wrapper (every OpenGL game it supports)
 *   - HelixMod-fixed DX9 games using the stereo-blit output pattern
 *   - NVIDIA 3D Vision SDK demos
 *   - Some "3D Vision Ready" titles in their stereo output path
 *
 * This path is mutually exclusive with the SetActiveEye capture: a game
 * doing magic-header SBS isn't also setting active eye. We co-exist with
 * the eye-state callback by populating the same m_leftEyeSurf / m_rightEyeSurf
 * slots from a different source.
 */

#pragma once

#include <d3d9.h>

namespace NvDirectMode {
namespace MagicHeader {

// Public NVIDIA constants from the 3D Vision SDK's nvstereo.h.
// Reproduced here so we don't take a build dep on the SDK.
constexpr unsigned int kSignature      = 0x4433564Eu;   // "NV3D"
constexpr unsigned int kFlagSwapEyes   = 0x00000001u;
constexpr unsigned int kFlagScaleToFit = 0x00000002u;
constexpr unsigned int kFlagScaleToFit2= 0x00000004u;

// NvStereoImageHeader (20 bytes, lives in the last row of a stereo surface)
struct NvStereoImageHeader
{
    unsigned int dwSignature;   // = kSignature
    unsigned int dwWidth;       // ignored by driver in practice
    unsigned int dwHeight;      // ignored
    unsigned int dwBPP;         // ignored
    unsigned int dwFlags;       // kFlag* combinations
};

struct DetectResult
{
    bool  hasMagic;
    bool  swapEyes;             // SIH_SWAP_EYES set in header flags
    UINT  eyeWidth;             // sourceWidth / 2
    UINT  eyeHeight;            // sourceHeight - 1 (the header occupies row [height])
};

// Inspect a source surface for the NVSTEREO_IMAGE_SIGNATURE magic in its last
// row. Returns hasMagic=false unless dimensions match (2W × H+1) AND the
// signature is found.
//
// Implementation prefers LockRect with D3DLOCK_READONLY (cheap, the kind of
// surface Helifax/HelixMod produce IS lockable on purpose); falls back to
// GetRenderTargetData→system-mem if the source isn't lockable.
DetectResult DetectStereoMagic(IDirect3DDevice9* pDevice,
                               IDirect3DSurface9* pSurface);

// Split a stereo source surface into two per-eye destination surfaces via
// StretchRect. Sources the LEFT half from rect [0..W, 0..H) and the RIGHT
// half from rect [W..2W, 0..H). If swapEyes is true, sources are flipped.
//
// pLeftDst / pRightDst must each be at least eyeWidth × eyeHeight. They can
// be larger (StretchRect handles scaling); we pass NULL dstRect to fill them.
HRESULT SplitStereoSurface(IDirect3DDevice9* pDevice,
                           IDirect3DSurface9* pSrc,
                           UINT eyeWidth, UINT eyeHeight,
                           IDirect3DSurface9* pLeftDst,
                           IDirect3DSurface9* pRightDst,
                           bool swapEyes);

} // namespace MagicHeader
} // namespace NvDirectMode
