/* IZ3D_FILE: $Id$ 
*
* Project : iZ3D Stereo Driver
* Copyright (C) iZ3D Inc. 2002 - 2010
*
* $Author$
* $Revision$
* $Date$
* $LastChangedBy$
* $URL$
*/
#include "StdAfx.h"
#include <algorithm>
#include <tchar.h>
#include <boost/unordered_map.hpp>
#include "Trace.h"
#include "Renderer.h"
#include <MinHook.h>

static HMODULE g_hRealOpenGL32 = NULL;

PFNGDIRELEASEDC pfnOrig_ReleaseDC = NULL;
PFNWGLCREATECONTEX pfnOrig_wglCreateContext = NULL;
PFNWGLDELETECONTEX pfnOrig_wglDeleteContext = NULL;
PFNWGLGETCURRENTDC pfnOrig_wglGetCurrentDC = NULL;
PFNWGLGETCURRENTCONTEX pfnOrig_wglGetCurrentContext = NULL;
PFNWGLMAKECURRENT pfnOrig_wglMakeCurrent = NULL;
PFNWGLVIEWPORT pfnOrig_glViewport = NULL;
PFNWGLSCISSOR pfnOrig_glScissor = NULL;
PFNWGLSWAPBUFFERS pfnOrig_wglSwapBuffers = NULL;
PFNWGLDRAWBUFFER pfnOrig_glDrawBuffer = NULL;
PFNWGLREADBUFFER pfnOrig_glReadBuffer = NULL;
PFNWGLCHOOSEPIXELFORMAT pfnOrig_wglChoosePixelFormat = NULL;
PFNWGLDESCRIBEPIXELFORMAT pfnOrig_wglDescribePixelFormat = NULL;
PFNGLGETBOOLEANV pfnOrig_glGetBooleanv = NULL;
PFNWGLGETPIXELFORMAT pfnOrig_wglGetPixelFormat = NULL;
PFNWGLSETPIXELFORMAT pfnOrig_wglSetPixelFormat = NULL;
PFNWGLAPICALL pfnOrig_wglGetProcAddress = NULL;

#define WGL_STEREO_ARB 0x2012
typedef BOOL (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef BOOL (WINAPI *PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC, int, int, UINT, const int*, int*);
static PFNWGLCHOOSEPIXELFORMATARBPROC      pfnReal_wglChoosePixelFormatARB      = NULL;
static PFNWGLGETPIXELFORMATATTRIBIVARBPROC pfnReal_wglGetPixelFormatAttribivARB = NULL;

typedef boost::unordered_map<HDC, bool> DCMap_t;
static DCMap_t g_StereoPixelFormat;

static Renderer *GetRenderer(HDC hdc)
{
	if (hdc == 0)
		return NULL;
	
	size_t nCount = g_RendererList.size();
	for (ULONG i = 0; i < nCount; i++)
	{
		if (g_RendererList[i].IsDCMatched(hdc))
		{
			Renderer *pRenderer = &g_RendererList[i];
			return pRenderer;
		}
	}
	
	if (GINFO_ENABLED)
	{
		bool StereoOn = false;
		DCMap_t::iterator iterDC = g_StereoPixelFormat.find(hdc);
		if (iterDC != g_StereoPixelFormat.end())
		{
			StereoOn = iterDC->second;
		}
		if(StereoOn)
		{
			DEBUG_TRACE1("Create new Renderer(hdc = 0x%X)\n", hdc);
			g_RendererList.push_back(Renderer());
			Renderer* pRenderer = &g_RendererList.back();
			BOOL bResult = pRenderer->SetApplicationDC(hdc);
			if (bResult)
				return pRenderer;
			else
			{
				DEBUG_MESSAGE("SetApplicationDC(hdc = 0x%X) returns FALSE\n", hdc);
				g_RendererList.pop_back();
				return NULL;
			}
		}
		else
			return NULL;
	} else
		return NULL;
}

BOOL WINAPI HwglSwapBuffers(HDC hdc)
{
	//gInfo.Trace = (gInfo.Debug && GetAsyncKeyState(VK_SCROLL) == 0x8001);
	DEBUG_TRACE2("wglSwapBuffers\n");
	Renderer *pRenderer = GetRenderer(hdc);
	if (pRenderer)
		return pRenderer->SwapBuffers();
	else
		return pfnOrig_wglSwapBuffers(hdc);
}

void WINAPI HglDrawBuffer(GLenum mode)
{
	DEBUG_TRACE2("glDrawBuffer(mode = %s)\n", GetDrawBufferFlagsString(mode));
#ifdef _DEBUG
	static std::string desc;
	desc = GetDrawBufferFlagsString(mode);
#endif
	HDC hdc = pfnOrig_wglGetCurrentDC();
	Renderer *pRenderer = GetRenderer(hdc);
	if (pRenderer)
		pRenderer->DrawBuffer(mode);
	else
		pfnOrig_glDrawBuffer (mode);
}

// Mono-HUD overlay candidate triggers. idTech 3 family games emit only two
// DrawBuffer calls per frame (BACK_LEFT, BACK_RIGHT), so overlay activation
// has to hook into some other GL call that fires between the right-eye 3D
// pass and the 2D HUD pass. The reliable candidates the engine touches when
// switching to 2D are:
//   glOrtho           — direct ortho setup (Q3 source uses it; some builds
//                       compute the matrix manually and bypass this)
//   glMatrixMode(PROJECTION) — projection-matrix switch right before loading
//                              the 2D matrix
//   glDisable(GL_DEPTH_TEST) — HUD always disables depth test
// We hook all three and any one fires the activation. The trace logger
// records which one triggered so we can prune the list per engine later.
typedef void (APIENTRY *PFN_glOrtho)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
typedef void (APIENTRY *PFN_glMatrixMode)(GLenum mode);
typedef void (APIENTRY *PFN_glDisable)(GLenum cap);
typedef void (APIENTRY *PFN_glColorMask)(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
PFN_glOrtho      pfnOrig_glOrtho      = NULL;
PFN_glMatrixMode pfnOrig_glMatrixMode = NULL;
PFN_glDisable    pfnOrig_glDisable    = NULL;
// Exposed via extern in Renderer.cpp so Renderer::ColorMask can call the real
// driver function (we force the mask to all-true rather than passing the
// engine's anaglyph mask through).
PFN_glColorMask  pfnOrig_glColorMask  = NULL;
// One-shot file logger used by the HUD-candidate hooks to confirm whether
// the hook is ever being entered. Each hook logs its first call regardless
// of the trace-window state. Lets us tell "hook installed but function not
// called" from "hook didn't install at all".
static void LogHookFirstCall(const char* tag)
{
	TCHAR path[MAX_PATH];
	_tcscpy_s(path, MAX_PATH, gInfo.DriverDirectory);
	_tcscat_s(path, MAX_PATH, _T("\\OpenGLQuadBufferStereo.log"));
	FILE* f = NULL;
	_tfopen_s(&f, path, _T("a"));
	if (!f) return;
	fprintf(f, "[wiz3D-OGL hook-firstcall] %s\n", tag);
	fflush(f);
	fclose(f);
}

void APIENTRY HglOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
	static volatile LONG firstCall = 0;
	if (InterlockedCompareExchange(&firstCall, 1, 0) == 0) LogHookFirstCall("glOrtho");
	HDC hdc = pfnOrig_wglGetCurrentDC();
	Renderer *pRenderer = GetRenderer(hdc);
	if (pRenderer)
		pRenderer->OnHudCandidate("glOrtho");
	if (pfnOrig_glOrtho)
		pfnOrig_glOrtho(left, right, bottom, top, zNear, zFar);
}
void APIENTRY HglMatrixMode(GLenum mode)
{
	static volatile LONG firstCall = 0;
	if (InterlockedCompareExchange(&firstCall, 1, 0) == 0) LogHookFirstCall("glMatrixMode");
	if (mode == GL_PROJECTION)
	{
		HDC hdc = pfnOrig_wglGetCurrentDC();
		Renderer *pRenderer = GetRenderer(hdc);
		if (pRenderer)
			pRenderer->OnHudCandidate("glMatrixMode(PROJECTION)");
	}
	if (pfnOrig_glMatrixMode)
		pfnOrig_glMatrixMode(mode);
}
void APIENTRY HglDisable(GLenum cap)
{
	static volatile LONG firstCall = 0;
	if (InterlockedCompareExchange(&firstCall, 1, 0) == 0) LogHookFirstCall("glDisable");
	if (cap == GL_DEPTH_TEST)
	{
		HDC hdc = pfnOrig_wglGetCurrentDC();
		Renderer *pRenderer = GetRenderer(hdc);
		if (pRenderer)
			pRenderer->OnHudCandidate("glDisable(GL_DEPTH_TEST)");
	}
	if (pfnOrig_glDisable)
		pfnOrig_glDisable(cap);
}

// Anaglyph Steal: when gInfo.AnaglyphSteal is set, route anaglyph-shaped masks
// through the Renderer (which redirects to per-eye PBuffers and forces the
// real mask to all-true). With the feature off, this is a thin passthrough.
void APIENTRY HglColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	static volatile LONG firstCall = 0;
	if (InterlockedCompareExchange(&firstCall, 1, 0) == 0) LogHookFirstCall("glColorMask");
	if (gInfo.AnaglyphSteal)
	{
		HDC hdc = pfnOrig_wglGetCurrentDC();
		Renderer *pRenderer = GetRenderer(hdc);
		if (pRenderer && pRenderer->ColorMask(r, g, b, a))
		{
			// Renderer handled the call (already pushed a forced all-true mask
			// to the driver). Don't double-apply the original mask.
			return;
		}
	}
	if (pfnOrig_glColorMask)
		pfnOrig_glColorMask(r, g, b, a);
}

BOOL WINAPI HwglMakeCurrent(HDC hdc, HGLRC hglrc)
{
	Renderer *pRenderer = GetRenderer(hdc);
	BOOL bResult;
	DEBUG_TRACE1("wglMakeCurrent(hdc = 0x%X, hglrc = 0x%X)\n", hdc, hglrc);
	if (pRenderer)
		bResult = pRenderer->MakeCurrent(hglrc);
	else
		bResult = pfnOrig_wglMakeCurrent (hdc, hglrc);
	return bResult;
}

HGLRC WINAPI HwglCreateContext(HDC hdc)
{
	HGLRC hContext = pfnOrig_wglCreateContext(hdc);
	DEBUG_TRACE1("wglCreateContext(hdc = 0x%X) returns 0x%X\n", hdc, hContext);
	return hContext;
}

BOOL WINAPI HwglDeleteContext(HGLRC hrc)
{
	BOOL bResult = pfnOrig_wglDeleteContext(hrc);
	DEBUG_TRACE1("wglDeleteContext(hrc = 0x%X) returns %d\n", hrc, bResult);
	return bResult;
}

int WINAPI HReleaseDC(HWND hWnd, HDC hDC)
{
	size_t nCount = g_RendererList.size();
	for (std::vector<Renderer>::iterator it = g_RendererList.begin(); it != g_RendererList.end(); it++)
	{
		if (it->IsDCMatched(hDC))
		{
			g_RendererList.erase(it);
		}
	}
	int nRes = pfnOrig_ReleaseDC(hWnd, hDC);
	DEBUG_TRACE1("ReleaseDC(hWnd = 0x%X, hDC = 0x%X) returns 0x%X\n", hWnd, hDC, nRes);
	return nRes;
}

HDC WINAPI HwglGetCurrentDC(VOID)
{
	HDC hDC = pfnOrig_wglGetCurrentDC();
	Renderer *pRenderer = GetRenderer(hDC);
	if (pRenderer && pRenderer->m_bGlInit)
	{
		hDC = pRenderer->GetApplicationDC();
	}
	DEBUG_TRACE2("wglGetCurrentDC() returns 0x%X\n", hDC);
	return hDC;
}

HGLRC WINAPI HwglGetCurrentContext(VOID)
{
	HGLRC hContext = pfnOrig_wglGetCurrentContext();
	HDC hDC = pfnOrig_wglGetCurrentDC();
	Renderer *pRenderer = GetRenderer(hDC);
	if (pRenderer && pRenderer->m_bGlInit)
	{
		hContext = pRenderer->GetApplicationContext();
	}
	DEBUG_TRACE2("wglGetCurrentContext() returns 0x%X\n", hContext);
	return hContext;
}

void WINAPI HglReadBuffer (GLenum mode)
{
#ifdef _DEBUG
	static std::string desc;
	desc = GetDrawBufferFlagsString(mode);
#endif
	pfnOrig_glReadBuffer(mode);
	DEBUG_TRACE1("glReadBuffer(mode = %s)\n", GetDrawBufferFlagsString(mode));
}

void WINAPI HglViewport (GLint x, GLint y, GLsizei width, GLsizei height)
{
	pfnOrig_glViewport (x, y, width, height);
	DEBUG_TRACE3("glViewport (x = %d, y = %d, width = %d, height = %d)\n", 
		x, y, width, height);
}

void WINAPI HglScissor (GLint x, GLint y, GLsizei width, GLsizei height)
{
	pfnOrig_glScissor (x, y, width, height);
	DEBUG_TRACE3("glScissor (x = %d, y = %d, width = %d, height = %d)\n", 
		x, y, width, height);
}

void WINAPI HglGetBooleanv(GLenum pname, GLboolean *params)
{
	pfnOrig_glGetBooleanv(pname, params);
	DEBUG_TRACE2("glGetBooleanv(pname = %d, *params = %d)\n", 
		pname, *params);
	if (GINFO_ENABLED && pname == GL_STEREO)
	{
		*params = TRUE;
	}
}

int WINAPI HwglChoosePixelFormat(HDC hdc, CONST PIXELFORMATDESCRIPTOR *pfd)
{
	int iPixelFormat = pfnOrig_wglChoosePixelFormat(hdc, pfd);
	if (pfd)
	{
		DEBUG_TRACE1("wglChoosePixelFormat(hdc = 0x%X, pfd->dwFlags = %s) returns %d\n", 
			hdc, GetPixelFormatDescriptorFlagsString(pfd->dwFlags), iPixelFormat);
	}
	if (iPixelFormat == 0)
	{
		DEBUG_TRACE1("GetLastErrorString() returns %s\n", GetLastErrorString());
	}
	return iPixelFormat;
}

BOOL WINAPI HwglSetPixelFormat(HDC hdc, int iPixelFormat, CONST PIXELFORMATDESCRIPTOR *pfd)
{
	bool bStereoOn = false;
	PIXELFORMATDESCRIPTOR new_pfd;
	if (GINFO_ENABLED)
	{
		if (pfd && ((pfd->dwFlags & PFD_STEREO) == PFD_STEREO))
		{
			bStereoOn = true;
			if (gInfo.EmulateQB)
			{
				new_pfd = *pfd;
				pfd = &new_pfd;
				new_pfd.dwFlags &= ~PFD_STEREO; 
			}
		} 
	}
	BOOL bResult = pfnOrig_wglSetPixelFormat(hdc, iPixelFormat, pfd);
	if (pfd)
	{
		DEBUG_TRACE1("wglSetPixelFormat(hdc = 0x%X, iPixelFormat = %d, pfd->dwFlags = %s) returns %d\n", 
			hdc, iPixelFormat, GetPixelFormatDescriptorFlagsString(pfd->dwFlags), bResult);
	}
	if (bStereoOn)
	{
		if (!gInfo.EmulateQB)
		{
			pfnOrig_wglDescribePixelFormat(hdc, iPixelFormat, 
				sizeof(PIXELFORMATDESCRIPTOR), &new_pfd); 
			bStereoOn = (new_pfd.dwFlags & PFD_STEREO) == PFD_STEREO;
		}
		if (bStereoOn)
		{
			g_StereoPixelFormat[hdc] = TRUE;
			DEBUG_TRACE1("g_iStereoPixelFormat = %d\n", iPixelFormat);
		}
	}
	if (bResult == 0)
	{
		DEBUG_TRACE1("GetLastErrorString() returns %s\n", GetLastErrorString());
	}
	return bResult;
}

int WINAPI HwglDescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd)
{
	int iMaxPixelFormat = pfnOrig_wglDescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
	if (ppfd)
	{
		DEBUG_TRACE2("wglDescribePixelFormat(hdc = 0x%X, iPixelFormat = %d, nBytes = %u, pfd->dwFlags = %s) returns %d\n", 
			hdc, iPixelFormat, nBytes, GetPixelFormatDescriptorFlagsString(ppfd->dwFlags), iMaxPixelFormat);
	}
	if (iMaxPixelFormat == 0)
	{
		DEBUG_TRACE2("GetLastErrorString() returns %s\n", GetLastErrorString());
	}
	if (ppfd && (ppfd->dwFlags & PFD_SUPPORT_OPENGL) && GINFO_ENABLED && gInfo.EmulateQB)
	{
		ppfd->dwFlags |= PFD_STEREO;
		DEBUG_TRACE2("\tnew pfd->dwFlags = %s\n", GetPixelFormatDescriptorFlagsString(ppfd->dwFlags));
	}
	return iMaxPixelFormat;
}

int WINAPI HwglGetPixelFormat(HDC hdc)
{
	int iPixelFormat = pfnOrig_wglGetPixelFormat(hdc);
	DEBUG_TRACE3("wglGetPixelFormat(hdc = 0x%X) returns %d\n", hdc, iPixelFormat);
	if (iPixelFormat == 0)
	{
		DEBUG_TRACE1("GetLastErrorString() returns %s\n", GetLastErrorString());
	}
	return iPixelFormat;
}

BOOL WINAPI HwglGetPixelFormatAttribivARB(HDC hdc, int iPixelFormat, int iLayerPlane,
	UINT nAttributes, const int* piAttributes, int* piValues)
{
	if (!pfnReal_wglGetPixelFormatAttribivARB)
		pfnReal_wglGetPixelFormatAttribivARB = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)
			pfnOrig_wglGetProcAddress("wglGetPixelFormatAttribivARB");
	BOOL bResult = pfnReal_wglGetPixelFormatAttribivARB
		? pfnReal_wglGetPixelFormatAttribivARB(hdc, iPixelFormat, iLayerPlane, nAttributes, piAttributes, piValues)
		: FALSE;
	if (GINFO_ENABLED && gInfo.EmulateQB && piAttributes && piValues)
		for (UINT i = 0; i < nAttributes; i++)
			if (piAttributes[i] == WGL_STEREO_ARB)
				piValues[i] = 1;
	return bResult ? bResult : TRUE;
}

BOOL WINAPI HwglChoosePixelFormatARB(HDC hdc, const int* piAttribIList,
	const FLOAT* pfAttribFList, UINT nMaxFormats, int* piFormats, UINT* nNumFormats)
{
	if (!pfnReal_wglChoosePixelFormatARB)
		pfnReal_wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)
			pfnOrig_wglGetProcAddress("wglChoosePixelFormatARB");
	if (!pfnReal_wglChoosePixelFormatARB) return FALSE;
	bool bStereoRequested = false;
	int modAttribs[64]; int modCount = 0;
	if (GINFO_ENABLED && gInfo.EmulateQB && piAttribIList)
	{
		const int* p = piAttribIList;
		while (p[0] != 0 && modCount < 62)
		{
			if (p[0] == WGL_STEREO_ARB) { if (p[1] != 0) bStereoRequested = true; }
			else { modAttribs[modCount++] = p[0]; modAttribs[modCount++] = p[1]; }
			p += 2;
		}
		modAttribs[modCount] = 0;
		piAttribIList = modAttribs;
	}
	BOOL bResult = pfnReal_wglChoosePixelFormatARB(hdc, piAttribIList, pfAttribFList,
		nMaxFormats, piFormats, nNumFormats);
	if (bStereoRequested && bResult && nNumFormats && *nNumFormats > 0)
	{
		g_StereoPixelFormat[hdc] = TRUE;
		DEBUG_TRACE1("wglChoosePixelFormatARB: stereo emulated for hdc=0x%X\n", hdc);
	}
	return bResult;
}

PROC WINAPI HwglGetProcAddress(LPCSTR lpszProcName)
{
	if (lpszProcName && GINFO_ENABLED && gInfo.EmulateQB)
	{
		if (strcmp(lpszProcName, "wglChoosePixelFormatARB") == 0)
			return (PROC)HwglChoosePixelFormatARB;
		if (strcmp(lpszProcName, "wglGetPixelFormatAttribivARB") == 0)
			return (PROC)HwglGetPixelFormatAttribivARB;
	}
	PROC proc = pfnOrig_wglGetProcAddress(lpszProcName);
	DEBUG_TRACE2("wglGetProcAddress(lpszProcName = %s) returns 0x%X\n", lpszProcName, proc);
	return proc;
}

extern "C" __declspec(dllexport) void WINAPI SetRealOpenGL32(HMODULE hMod)
{
	g_hRealOpenGL32 = hMod;
}

BOOL WINAPI HookOGL()
{
	MH_CreateHookApi(L"gdi32.dll", "ReleaseDC", HReleaseDC, (LPVOID*)&pfnOrig_ReleaseDC);

	if (g_hRealOpenGL32)
	{
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "glGetBooleanv"), (LPVOID)HglGetBooleanv, (LPVOID*)&pfnOrig_glGetBooleanv);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglChoosePixelFormat"), (LPVOID)HwglChoosePixelFormat, (LPVOID*)&pfnOrig_wglChoosePixelFormat);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "glViewport"), (LPVOID)HglViewport, (LPVOID*)&pfnOrig_glViewport);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "glScissor"), (LPVOID)HglScissor, (LPVOID*)&pfnOrig_glScissor);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "glDrawBuffer"), (LPVOID)HglDrawBuffer, (LPVOID*)&pfnOrig_glDrawBuffer);
		// Log MinHook status for the HUD-detection candidates so we can tell
		// install success from "hooks installed but functions never called".
		// FP_ADDR for log clarity: also log the address each hook is patching.
		{
			LPVOID addrOrtho      = (LPVOID)GetProcAddress(g_hRealOpenGL32, "glOrtho");
			LPVOID addrMatrixMode = (LPVOID)GetProcAddress(g_hRealOpenGL32, "glMatrixMode");
			LPVOID addrDisable    = (LPVOID)GetProcAddress(g_hRealOpenGL32, "glDisable");
			MH_STATUS rcO = MH_CreateHook(addrOrtho,      (LPVOID)HglOrtho,      (LPVOID*)&pfnOrig_glOrtho);
			MH_STATUS rcM = MH_CreateHook(addrMatrixMode, (LPVOID)HglMatrixMode, (LPVOID*)&pfnOrig_glMatrixMode);
			MH_STATUS rcD = MH_CreateHook(addrDisable,    (LPVOID)HglDisable,    (LPVOID*)&pfnOrig_glDisable);
			TCHAR path[MAX_PATH];
			_tcscpy_s(path, MAX_PATH, gInfo.DriverDirectory);
			_tcscat_s(path, MAX_PATH, _T("\\OpenGLQuadBufferStereo.log"));
			FILE* f = NULL;
			_tfopen_s(&f, path, _T("a"));
			if (f) {
				fprintf(f, "[wiz3D-OGL hook-status] glOrtho:      addr=%p rc=%d\n", addrOrtho,      (int)rcO);
				fprintf(f, "[wiz3D-OGL hook-status] glMatrixMode: addr=%p rc=%d\n", addrMatrixMode, (int)rcM);
				fprintf(f, "[wiz3D-OGL hook-status] glDisable:    addr=%p rc=%d\n", addrDisable,    (int)rcD);
				fflush(f); fclose(f);
			}
		}
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "glReadBuffer"), (LPVOID)HglReadBuffer, (LPVOID*)&pfnOrig_glReadBuffer);
		// Anaglyph Steal (gInfo.AnaglyphSteal): hook glColorMask so the
		// Renderer can detect engines' anaglyph stereo patterns and reroute
		// per-eye draws into PBuffer eyes. Hook installs unconditionally so
		// toggling AnaglyphSteal at runtime works; the gating happens inside
		// HglColorMask. Cost when off is one branch + indirect call per
		// glColorMask — negligible.
		{
			LPVOID addrCM = (LPVOID)GetProcAddress(g_hRealOpenGL32, "glColorMask");
			MH_STATUS rcCM = MH_CreateHook(addrCM, (LPVOID)HglColorMask, (LPVOID*)&pfnOrig_glColorMask);
			TCHAR path[MAX_PATH];
			_tcscpy_s(path, MAX_PATH, gInfo.DriverDirectory);
			_tcscat_s(path, MAX_PATH, _T("\\OpenGLQuadBufferStereo.log"));
			FILE* f = NULL;
			_tfopen_s(&f, path, _T("a"));
			if (f) {
				fprintf(f, "[wiz3D-OGL hook-status] glColorMask: addr=%p rc=%d (anaglyph-steal=%u)\n",
					addrCM, (int)rcCM, gInfo.AnaglyphSteal);
				fflush(f); fclose(f);
			}
		}
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglCreateContext"), (LPVOID)HwglCreateContext, (LPVOID*)&pfnOrig_wglCreateContext);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglDeleteContext"), (LPVOID)HwglDeleteContext, (LPVOID*)&pfnOrig_wglDeleteContext);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglGetCurrentContext"), (LPVOID)HwglGetCurrentContext, (LPVOID*)&pfnOrig_wglGetCurrentContext);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglGetCurrentDC"), (LPVOID)HwglGetCurrentDC, (LPVOID*)&pfnOrig_wglGetCurrentDC);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglMakeCurrent"), (LPVOID)HwglMakeCurrent, (LPVOID*)&pfnOrig_wglMakeCurrent);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglSwapBuffers"), (LPVOID)HwglSwapBuffers, (LPVOID*)&pfnOrig_wglSwapBuffers);
		//MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglSwapLayerBuffers"), (LPVOID)HwglSwapLayerBuffers, (LPVOID*)&pfnOrig_wglSwapLayerBuffers);
		//MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglSwapMultipleBuffers"), (LPVOID)HwglSwapMultipleBuffers, (LPVOID*)&pfnOrig_wglSwapMultipleBuffers);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglDescribePixelFormat"), (LPVOID)HwglDescribePixelFormat, (LPVOID*)&pfnOrig_wglDescribePixelFormat);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglGetPixelFormat"), (LPVOID)HwglGetPixelFormat, (LPVOID*)&pfnOrig_wglGetPixelFormat);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglSetPixelFormat"), (LPVOID)HwglSetPixelFormat, (LPVOID*)&pfnOrig_wglSetPixelFormat);
		MH_CreateHook((LPVOID)GetProcAddress(g_hRealOpenGL32, "wglGetProcAddress"), (LPVOID)HwglGetProcAddress, (LPVOID*)&pfnOrig_wglGetProcAddress);
	}

	MH_EnableHook(MH_ALL_HOOKS);
	return TRUE;
}
