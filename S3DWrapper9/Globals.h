/* 
* Project : iZ3D Stereo Driver
* Copyright (C) iZ3D Inc. 2002 - 2010
*/

#pragma once

#include "..\S3DAPI\GlobalData.h"
#include "GlobalData.h"
#include "CallGuard.h"

inline bool IsLeftViewUnmodified() {
	return gInfo.SeparationMode == 1;
}
inline bool IsRightViewUnmodified() {
	return gInfo.SeparationMode == 2;
}

#define	IS_D3D9EX_PRESENTER					(USE_MULTI_DEVICE_PRESENTER || USE_UM_PRESENTER_D3D9EX)

void InitDirectories();
void EnsureWrapperInitialized();

// Direct appender to wiz3D_proxy.log (next to the game exe). Mirrors the
// DDILog pattern from the DX10/11 wrapper — the DX9 wrapper's own ZLOg
// stream goes to a separate location, so tester reports of crashes only
// have the proxy log to work with. Use for high-value diagnostic lines
// (Direct3DCreate9 serial, QueryInterface bypass risk, wrapper destruction)
// that need to show up alongside the proxy's own log entries.
void D9Log(const char* fmt, ...);

// IID → name lookup for D3D9-side QI fall-through logging. Recognises the
// IDirect3D9 / Ex / IUnknown family; unknowns fall back to hex GUID form.
// Mirror of FormatGUID in S3DWrapper10/AdapterFunctions.cpp.
void FormatD9IID(REFIID riid, char* buf, size_t bufLen);

// Process-wide lock for g_pDirectWrapperList. GRFS-style probe-heavy games
// call Direct3DCreate9 multiple times in quick succession; without
// serialisation, the std::vector inside WrapperList races between Add /
// Remove / Find when two threads do this concurrently. Initialised in
// DllMain and held by AddWrapper / RemoveWrapper / FindWrapper callers.
extern CRITICAL_SECTION g_DirectWrapperListLock;

//--- GDI GetDeviceGammaRamp/GetDeviceGammaRamp manage section ---
BOOL WINAPI Hooked_SetDeviceGammaRamp(HDC hDC, LPVOID lpRamp);
BOOL WINAPI Hooked_GetDeviceGammaRamp(HDC hDC, LPVOID lpRamp);

extern BOOL (WINAPI *Original_SetDeviceGammaRamp)(HDC hDC, LPVOID lpRamp);
extern BOOL (WINAPI *Original_GetDeviceGammaRamp)(HDC hDC, LPVOID lpRamp);

extern D3DGAMMARAMP g_DeviceGammaRamp;
extern bool g_bGammaRampInitialized;
void InitGammaRamp();
