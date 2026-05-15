/*
* Project : iZ3D Stereo Driver
* Simulated Reality Weave Output for Samsung Odyssey 3D / Acer SpatialLabs
* Copyright (C) iZ3D Inc. 2002 - 2025
*/

#include "stdafx.h"
#include "Output_dx9.h"
#include "S3DWrapper9\BaseSwapChain.h"
#include <tinyxml.h>

// SR SDK headers — identical include paths for both platforms:
// x64: LeiaSR-SDK-1.36.2-win64  (IDX9Weaver1 / CreateDX9Weaver factory API)
// Win32: simulatedreality-1.34.10-win32-Release  (same IDX9Weaver1 / CreateDX9Weaver factory API)
#include "sr/weaver/dx9weaver.h"
#include "sr/management/srcontext.h"
#include "sr/utility/exception.h"

using namespace DX9Output;

#ifdef _MANAGED
#pragma managed(push, off)
#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

#ifdef _MANAGED
#pragma managed(pop)
#endif

OUTPUT_API void* CALLBACK CreateOutputDX9(DWORD mode, DWORD spanMode)
{
    return new SimulatedRealityWeaveOutput(mode, spanMode);
}

OUTPUT_API void* CALLBACK CreateOutputDX10(DWORD mode, DWORD spanMode)
{
    return nullptr;
}

OUTPUT_API void* CALLBACK CreateOutputDX11(DWORD mode, DWORD spanMode)
{
    return nullptr;
}

OUTPUT_API DWORD CALLBACK GetOutputCaps()
{
    return 0;
}

OUTPUT_API void CALLBACK GetOutputName(char* name, DWORD size)
{
    strcpy_s(name, size, "SimulatedRealityWeave");
}

OUTPUT_API BOOL CALLBACK EnumOutputModes(DWORD num, char* name, DWORD size)
{
    switch(num)
    {
    case 0:
        strcpy_s(name, size, "Default");
        return TRUE;
    default:
        return FALSE;
    }
}

SimulatedRealityWeaveOutput::SimulatedRealityWeaveOutput(DWORD mode, DWORD spanMode)
    : OutputMethod(mode, spanMode)
    , m_Weaver(nullptr)
    , m_pSRContext(nullptr)
    , m_pSBSTexture(nullptr)
    , m_ViewWidth(0)
    , m_ViewHeight(0)
    , m_ViewFormat(D3DFMT_A8R8G8B8)
    , m_WeaverInitialized(false)
    , m_WeavingEnabled(true)
    , m_bSRGB(true)        // Default sRGB on — matches SR SDK directx9_weaving sample (eColorSpace::sRGBHardware) and modern engines (Source, Unreal, Unity)
    , m_bSRFallbackActive(false)
    // CBaseStereoRenderer::CreateOutput is followed immediately by a
    // StereoModeChanged(m_Input.StereoActive != 0) call in the renderer
    // ctor (BaseStereoRenderer.cpp:245), so the value here is overwritten
    // before the first Output() runs. Default true matches the previous
    // behaviour (weaver lazy-inits on first Output) for the unlikely case
    // where StereoModeChanged is somehow skipped.
    , m_bStereoActive(true)
{
}

SimulatedRealityWeaveOutput::~SimulatedRealityWeaveOutput(void)
{
    CleanupWeaver();
}

UINT SimulatedRealityWeaveOutput::GetOutputChainsNumber()
{
    return 1;
}

HRESULT SimulatedRealityWeaveOutput::InitializeSCData(CBaseSwapChain* pSwapChain)
{
    if (!pSwapChain)
        return E_INVALIDARG;

    HRESULT hr = OutputMethod::InitializeSCData(pSwapChain);
    if (FAILED(hr))
        return hr;

    // Release existing SBS texture — will be recreated below for new dimensions
    if (m_pSBSTexture) {
        m_pSBSTexture->Release();
        m_pSBSTexture = nullptr;
    }
    // Invalidate weaver D3D pool resources so they can be recreated at new dimensions
    if (m_WeaverInitialized) {
        if (m_Weaver)
            m_Weaver->invalidateDeviceObjects();
        m_WeaverInitialized = false;
    }

    if (!m_pd3dDevice || !pSwapChain->m_pPrimaryBackBuffer)
        return S_OK;

    D3DSURFACE_DESC desc;
    if (SUCCEEDED(pSwapChain->m_pPrimaryBackBuffer->GetDesc(&desc))) {
        m_ViewWidth  = desc.Width;
        m_ViewHeight = desc.Height;
        m_ViewFormat = desc.Format;

        // SBS texture: left eye occupies [0, ViewWidth), right eye [ViewWidth, ViewWidth*2)
        m_pd3dDevice->CreateTexture(
            m_ViewWidth * 2, m_ViewHeight,
            1, D3DUSAGE_RENDERTARGET, m_ViewFormat,
            D3DPOOL_DEFAULT, &m_pSBSTexture, nullptr);
    }

    return S_OK;
}

HRESULT SimulatedRealityWeaveOutput::InitializeWeaver(IDirect3DDevice9* pDevice, HWND hWnd, UINT width, UINT height)
{
    // Tear down any previously created weaver (re-init path after device reset / resize)
    if (m_Weaver) {
        m_Weaver->destroy();
        m_Weaver = nullptr;
    }
    if (m_pSRContext) {
        SR::SRContext::deleteSRContext(m_pSRContext);
        m_pSRContext = nullptr;
    }

    try {
        // NonBlockingClientMode: connects once, throws ServerNotAvailableException if SR Service is not running
        m_pSRContext = SR::SRContext::create();
    }
    catch (const SR::ServerNotAvailableException&) {
        return E_FAIL;
    }
    catch (...) {
        return E_FAIL;
    }

    // IDX9Weaver1 factory API (SDK 1.36.2 x64 / SDK 1.34.10 Win32): no explicit SBS dimensions needed
    WeaverErrorCode result = SR::CreateDX9Weaver(m_pSRContext, pDevice, hWnd, &m_Weaver);
    if (result != WeaverSuccess) {
        SR::SRContext::deleteSRContext(m_pSRContext);
        m_pSRContext = nullptr;
        return E_FAIL;
    }

    // initialize() must be called after all weavers/senses are created
    m_pSRContext->initialize();
    m_WeaverInitialized = true;
    return S_OK;
}

// Called by the wrapper's StopEngine() before each device Reset (and during
// shutdown). The base OutputMethod::Clear() doesn't know about SR-specific
// resources, so without this override the weaver and our DEFAULT-pool SBS
// texture would survive Reset as dangling pointers - next Output() crashes
// inside m_Weaver->weave() when SR tries to bind its now-invalid vertex
// shader (Max Payne 3 -stereo 1 reproducer, 2026-05-08). After Clear,
// the wrapper's StartEngine() calls Initialize()/InitializeSCData() to
// rebuild for the new device state.
void SimulatedRealityWeaveOutput::Clear()
{
	CleanupWeaver();
	if (m_pSBSTexture)
	{
		m_pSBSTexture->Release();
		m_pSBSTexture = nullptr;
	}
	// Reset the SR-fallback decision on Clear so post-device-reset (e.g. windowed↔
	// fullscreen toggle) we get a fresh chance to detect the SR runtime. If the
	// user installed the LeiaSR Service mid-session, this is when it gets picked up.
	m_bSRFallbackActive = false;
	OutputMethod::Clear();
}

void SimulatedRealityWeaveOutput::CleanupWeaver()
{
    if (m_Weaver) {
        m_Weaver->destroy();
        m_Weaver = nullptr;
    }
    if (m_pSRContext) {
        SR::SRContext::deleteSRContext(m_pSRContext);
        m_pSRContext = nullptr;
    }
    if (m_pSBSTexture) {
        m_pSBSTexture->Release();
        m_pSBSTexture = nullptr;
    }
    m_WeaverInitialized = false;
}

// Toggle handler for stereo on/off. On going-mono we destroy the SR weaver
// entirely (rather than just skipping weave() calls) because the weaver's
// constructor latches the SR microlens panel and eye-tracking camera into
// active state; only destroy() releases them. The next stereo-on transition
// trips Output()'s existing lazy-init path on the very next frame, so the
// weaver / lens / camera all come back without an explicit re-create call.
void SimulatedRealityWeaveOutput::StereoModeChanged(bool bNewMode)
{
    if (m_bStereoActive == bNewMode)
        return;
    m_bStereoActive = bNewMode;
    if (!bNewMode && m_WeaverInitialized)
        CleanupWeaver();
}

HRESULT SimulatedRealityWeaveOutput::Output(CBaseSwapChain* pSwapChain)
{
    if (!pSwapChain || !m_WeavingEnabled)
        return S_OK;

    if (!m_pd3dDevice)
        return S_OK;

    // Skip weaver work (including lazy init) while in mono mode. Without
    // this, the next Output() after toggling off would re-create the
    // weaver and re-light the SR microlens panel + camera, defeating
    // StereoModeChanged(false)'s teardown.
    if (!m_bStereoActive)
        return S_OK;

    // Lazy-initialize the weaver on the first Output call (device and window are ready by then).
    // If the LeiaSR runtime isn't installed, the SR Service isn't running, or no SR display is
    // connected, InitializeWeaver fails — we then sticky-flag the session into SBS fallback mode
    // so users on non-SR hardware still see a usable stereo image (plain Half SBS) instead of a
    // black frame, and we don't keep retrying SR every Present.
    if (!m_WeaverInitialized && !m_bSRFallbackActive) {
        HWND hWnd = pSwapChain->GetAppWindow();
        HRESULT hr = InitializeWeaver(m_pd3dDevice, hWnd, m_ViewWidth, m_ViewHeight);
        if (FAILED(hr)) {
            m_bSRFallbackActive = true;
            OutputDebugStringA("[SimulatedRealityWeaveOutput] SR runtime unavailable — falling back to Half SBS for this session.\n");
        }
    }

    if (m_bSRFallbackActive)
        return OutputSBSFallback(pSwapChain);

    if (!m_Weaver || !m_pSBSTexture)
        return S_OK;

    IDirect3DSurface9* pLeft  = pSwapChain->GetLeftBackBufferRT();
    IDirect3DSurface9* pRight = pSwapChain->GetRightBackBufferRT();
    RECT* pLeftRect  = pSwapChain->GetLeftBackBufferRect();
    RECT* pRightRect = pSwapChain->GetRightBackBufferRect();

    if (!pLeft || !pRight)
        return S_OK;

    IDirect3DSurface9* pSBSSurface = nullptr;
    if (FAILED(m_pSBSTexture->GetSurfaceLevel(0, &pSBSSurface)))
        return S_OK;

    // Blit left eye into left half and right eye into right half of the SBS texture
    RECT leftDst  = { 0,                 0, (LONG)m_ViewWidth,     (LONG)m_ViewHeight };
    RECT rightDst = { (LONG)m_ViewWidth, 0, (LONG)m_ViewWidth * 2, (LONG)m_ViewHeight };

    m_pd3dDevice->StretchRect(pLeft,  pLeftRect,  pSBSSurface, &leftDst,  D3DTEXF_NONE);
    m_pd3dDevice->StretchRect(pRight, pRightRect, pSBSSurface, &rightDst, D3DTEXF_NONE);
    pSBSSurface->Release();

    // Route weaved output to the primary back buffer
    m_pd3dDevice->SetRenderTarget(0, pSwapChain->m_pPrimaryBackBuffer);

    // State-block save/restore: the Dimenco/LeiaSR DX9 weaver mutates D3D9
    // device state (shaders, samplers, render states, vertex decl) and does
    // not restore it. Re-applying our D3DSBT_ALL state block after weave()
    // returns puts the device back to what the wrapper expects, so the
    // wrapper's post-Present code sees the same state it set up.
    IDirect3DStateBlock9* pSavedState = nullptr;
    HRESULT hrSB = m_pd3dDevice->CreateStateBlock(D3DSBT_ALL, &pSavedState);

    // SEH safety net around weave(). On Tales of Berseria x64 we see SR's
    // weave() body call back into our wrapper's SetVertexShader with a
    // pointer that crashes inside d3d9.dll +0x258FF (READ of -1). With this
    // crash mid-weave the state-block Apply() never runs, the wrapper sees
    // the corrupted state on the next frame, and the game dies. Wrapping
    // the weave call in __try/__except lets us catch the AV here, mark the
    // session as SR-failed, and fall back to plain Half SBS for the rest of
    // the session so the game keeps running. The session stays in SBS even
    // if the next frame's weave() would have succeeded — once SR has shown
    // it can crash this device, we trust it not at all for the duration.
    bool weaveOK = SafeWeave(m_Weaver, m_pSBSTexture,
                             m_ViewWidth, m_ViewHeight, m_ViewFormat, m_bSRGB);

    if (SUCCEEDED(hrSB) && pSavedState)
    {
        pSavedState->Apply();
        pSavedState->Release();
    }

    if (!weaveOK)
    {
        OutputDebugStringA("[SimulatedRealityWeaveOutput] weave() raised AV — disabling SR for this session, falling back to Half SBS.\n");
        m_bSRFallbackActive = true;
        return OutputSBSFallback(pSwapChain);
    }

    return S_OK;
}

// SEH-protected weave() invocation. Factored into its own function because
// __try/__except cannot share scope with C++ try/catch (which the surrounding
// Output() / InitializeWeaver have elsewhere in this TU). Returns true on
// success, false if SR's weave() raised an access violation (or any other
// fatal SEH exception — we treat all of those as "SR is broken on this
// device, switch to SBS").
bool SimulatedRealityWeaveOutput::SafeWeave(
    SR::IDX9Weaver1* weaver,
    IDirect3DTexture9* sbsTexture,
    UINT width, UINT height, D3DFORMAT format, bool isSRGB)
{
    __try {
        weaver->setInputViewTexture(sbsTexture, width, height, format, isSRGB);
        weaver->setOutputSRGBWrite(isSRGB);
        weaver->weave();
        return true;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                ? EXCEPTION_EXECUTE_HANDLER
                : EXCEPTION_CONTINUE_SEARCH)
    {
        return false;
    }
}

// Plain Half SBS rendering, invoked when SR weave is unavailable. Mirrors the
// minimum-viable pixel path of SideBySideOutput::Output() (mode 0, no gap, no
// crosseyed) — left eye into the left half of the primary back buffer, right
// eye into the right half. Two StretchRect calls, no shaders, no extra state.
HRESULT SimulatedRealityWeaveOutput::OutputSBSFallback(CBaseSwapChain* pSwapChain)
{
    if (!pSwapChain || !m_pd3dDevice) return S_OK;
    IDirect3DSurface9* primary = pSwapChain->m_pPrimaryBackBuffer;
    if (!primary) return S_OK;

    IDirect3DSurface9* pLeft  = pSwapChain->GetLeftBackBufferRT();
    IDirect3DSurface9* pRight = pSwapChain->GetRightBackBufferRT();
    if (!pLeft || !pRight) return S_OK;
    RECT* pLeftRect  = pSwapChain->GetLeftBackBufferRect();
    RECT* pRightRect = pSwapChain->GetRightBackBufferRect();

    D3DSURFACE_DESC desc;
    primary->GetDesc(&desc);
    RECT leftHalf  = { 0,                       0, (LONG)(desc.Width / 2), (LONG)desc.Height };
    RECT rightHalf = { (LONG)(desc.Width / 2),  0, (LONG)desc.Width,       (LONG)desc.Height };
    m_pd3dDevice->StretchRect(pLeft,  pLeftRect,  primary, &leftHalf,  D3DTEXF_LINEAR);
    m_pd3dDevice->StretchRect(pRight, pRightRect, primary, &rightHalf, D3DTEXF_LINEAR);
    return S_OK;
}

void SimulatedRealityWeaveOutput::ReadConfigData(TiXmlNode* config)
{
    // Optional override under <Outputs><SimulatedRealityWeaveOutput><sRGB Value="0|1"/>.
    // Default (no element) keeps m_bSRGB at the constructor value (true).
    if (!config)
        return;
    TiXmlElement* el = config->FirstChildElement("sRGB");
    if (el)
    {
        int v = 1;
        if (el->QueryIntAttribute("Value", &v) == TIXML_SUCCESS)
            m_bSRGB = (v != 0);
    }
}
