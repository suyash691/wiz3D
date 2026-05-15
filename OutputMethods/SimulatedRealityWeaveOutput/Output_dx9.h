/*
* Project : iZ3D Stereo Driver
* Simulated Reality Weave Output for Samsung Odyssey 3D / Acer SpatialLabs
* Copyright (C) iZ3D Inc. 2002 - 2025
*/

#pragma once

#include "OutputMethod_dx9.h"

// Forward declarations — both x64 (SDK 1.36.2) and Win32 (SDK 1.34.10) use IDX9Weaver1
namespace SR {
    class IDX9Weaver1;
    class SRContext;
}

namespace DX9Output
{

class SimulatedRealityWeaveOutput :
    public OutputMethod
{
public:
    SimulatedRealityWeaveOutput(DWORD mode, DWORD spanMode);
    virtual ~SimulatedRealityWeaveOutput(void);

    virtual UINT    GetOutputChainsNumber();
    virtual HRESULT Output(CBaseSwapChain* pSwapChain);
    virtual HRESULT InitializeSCData(CBaseSwapChain* pSwapChain);
    virtual void    Clear();
    virtual void    ReadConfigData(TiXmlNode* config);
    // CBaseStereoRenderer::Present calls this whenever StereoActive toggles
    // (NvAPI bridge from a 3D Vision-aware game, or the wiz3D * hotkey).
    // We tear the SR weaver fully down when going mono so the SR microlens
    // panel and the eye-tracking camera stop drawing power — leaving them
    // running while the game is rendering a flat image is a visible nuisance
    // on Samsung Odyssey 3D / Acer SpatialLabs hardware. Going stereo lets
    // Output()'s existing lazy-init re-create the weaver on the next frame.
    virtual void    StereoModeChanged(bool bNewMode);

private:
    SR::IDX9Weaver1*   m_Weaver;
    SR::SRContext*     m_pSRContext;
    IDirect3DTexture9* m_pSBSTexture;
    UINT               m_ViewWidth;
    UINT               m_ViewHeight;
    D3DFORMAT          m_ViewFormat;
    bool               m_WeaverInitialized;
    bool               m_WeavingEnabled;
    bool               m_bSRGB;             // Treat input as sRGB and write sRGB output. Default true (modern games / SR sample default). Override per-game via config.
    bool               m_bSRFallbackActive; // Sticky: set to true when InitializeWeaver fails (no LeiaSR runtime / no SR display / SR Service down). Once set, Output() renders plain Half SBS instead of attempting SR weave again.
    bool               m_bStereoActive;     // Last value seen from StereoModeChanged. Output() returns early when false so we neither lazy-init nor weave while the game is in mono mode.

    HRESULT InitializeWeaver(IDirect3DDevice9* pDevice, HWND hWnd, UINT width, UINT height);
    void    CleanupWeaver();
    HRESULT OutputSBSFallback(CBaseSwapChain* pSwapChain);  // Plain Half SBS (two StretchRects into primary BB) — invoked when SR weave is unavailable
    // SEH-protected weaver invocation. Factored out of Output() so __try/__except
    // doesn't share scope with C++ try/catch elsewhere in the .cpp. Returns
    // false on access violation; caller treats that as "SR is broken on this
    // device" and switches to SBS for the rest of the session.
    static bool SafeWeave(SR::IDX9Weaver1* weaver, IDirect3DTexture9* sbsTexture,
                          UINT width, UINT height, D3DFORMAT format, bool isSRGB);
};

}
