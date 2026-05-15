#include "stdafx.h"
#include "Wiz3DStereoApi.h"
#include "BaseStereoRenderer.h"
#include "..\S3DAPI\GlobalData.h"
#include "..\S3DAPI\KeyboardHook.h" // gKbdHook.m_Access lock + extern decl
#include "..\S3DAPI\ReadData.h"     // WriteInputData for UserProfile.xml persistence

// Renderer-presence sentinel. We don't actually mutate the renderer's
// m_Input — that gets overwritten from gInfo.Input every Present (see
// BaseStereoRenderer_Main.cpp around line 305: `m_Input = updatedInput`,
// where updatedInput came from gInfo.Input). The * hotkey and our bridge
// must both write to gInfo.Input or their changes disappear on the next
// frame. This pointer just tells us the wrapper is initialised enough
// to honour state changes; before that, NvApi callers see static
// defaults so they don't bail out (HelixMod is unforgiving here).
static CBaseStereoRenderer* g_pActiveRenderer = nullptr;

extern "C"
{

void Wiz3D_RegisterActiveRenderer(CBaseStereoRenderer* pRenderer)
{
	g_pActiveRenderer = pRenderer;
}

void Wiz3D_UnregisterActiveRenderer(CBaseStereoRenderer* pRenderer)
{
	if (g_pActiveRenderer == pRenderer)
		g_pActiveRenderer = nullptr;
}

// Defaults returned before the wrapper is fully initialised. Chosen so
// 3D Vision-aware games / HelixMod don't bail out on a "stereo isn't
// usable" probe at startup:
//   - IsActivated = 1   : caller proceeds with stereo setup
//   - Separation  = 50% : neutral mid-slider value
//   - Convergence = 1.0 : 1 world-unit depth

int Wiz3D_GetStereoActive()
{
	if (!g_pActiveRenderer)
		return 1;
	return gInfo.Input.StereoActive ? 1 : 0;
}

void Wiz3D_SetStereoActive(int active)
{
	if (!g_pActiveRenderer)
		return; // wrapper not alive yet; caller will retry once it is
	CriticalSectionLocker locker(gKbdHook.m_Access);
	gInfo.Input.StereoActive = active ? true : false;
	WriteInputData(&gInfo.Input);
}

float Wiz3D_GetSeparationPercent()
{
	if (!g_pActiveRenderer)
		return 50.0f;
	const CameraPreset* p = gInfo.Input.GetActivePreset();
	return SEPARATION_TO_PERCENT(p->StereoBase);
}

void Wiz3D_SetSeparationPercent(float percent)
{
	if (!g_pActiveRenderer)
		return;
	if (percent < 0.0f) percent = 0.0f;
	if (percent > 100.0f) percent = 100.0f;
	CriticalSectionLocker locker(gKbdHook.m_Access);
	CameraPreset* p = gInfo.Input.GetActivePreset();
	p->StereoBase = PERCENT_TO_SEPARATION(percent);
	WriteInputData(&gInfo.Input);
}

float Wiz3D_GetConvergence()
{
	if (!g_pActiveRenderer)
		return 1.0f;
	const CameraPreset* p = gInfo.Input.GetActivePreset();
	if (p->One_div_ZPS == 0.0f)
		return 1.0f;
	return 1.0f / p->One_div_ZPS;
}

void Wiz3D_SetConvergence(float depth)
{
	if (!g_pActiveRenderer)
		return;
	if (depth <= 0.0f)
		return;
	CriticalSectionLocker locker(gKbdHook.m_Access);
	CameraPreset* p = gInfo.Input.GetActivePreset();
	p->One_div_ZPS = 1.0f / depth;
	WriteInputData(&gInfo.Input);
}

void Wiz3D_StepSeparation(int dir)
{
	if (!g_pActiveRenderer)
		return;
	CriticalSectionLocker locker(gKbdHook.m_Access);
	CameraPreset* p = gInfo.Input.GetActivePreset();
	float step = (dir > 0) ? STEP_STEREOBASE : -STEP_STEREOBASE;
	p->StereoBase += step;
	if (p->StereoBase < 0.0f) p->StereoBase = 0.0f;
	if (p->StereoBase > MAX_STEREOBASE) p->StereoBase = MAX_STEREOBASE;
	WriteInputData(&gInfo.Input);
}

void Wiz3D_StepConvergence(int dir)
{
	// "Increase convergence" pushes parallax plane FURTHER away (Z_conv up).
	// Since One_div_ZPS = 1/Z_conv, that maps to One_div_ZPS DECREASING.
	if (!g_pActiveRenderer)
		return;
	CriticalSectionLocker locker(gKbdHook.m_Access);
	CameraPreset* p = gInfo.Input.GetActivePreset();
	float step = (dir > 0) ? -STEP_ONE_DIV_ZPS : STEP_ONE_DIV_ZPS;
	p->One_div_ZPS += step;
	if (p->One_div_ZPS < MIN_ONE_DIV_ZPS) p->One_div_ZPS = MIN_ONE_DIV_ZPS;
	if (p->One_div_ZPS > MAX_ONE_DIV_ZPS) p->One_div_ZPS = MAX_ONE_DIV_ZPS;
	WriteInputData(&gInfo.Input);
}

int Wiz3D_HasProfileEntry()
{
	// gInfo flags are set during ReadProfilesAllParts when the running game's
	// exe matches an entry under <Profile><File Name="..."> in any of the
	// three profile files. No renderer required — these reflect static load
	// state from process startup, even if the renderer hasn't registered yet.
	return (gInfo.bMatchedInBase || gInfo.bMatchedInCommunity || gInfo.bMatchedInUser) ? 1 : 0;
}

} // extern "C"
