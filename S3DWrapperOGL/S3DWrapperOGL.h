/* 
* Project : iZ3D Stereo Driver
* Copyright (C) iZ3D Inc. 2002 - 2010
*/

#pragma once

#include <glh/glh_extensions.h>
#include <GL/glext.h>

#ifdef S3DWRAPPEROGL_EXPORTS
#define S3DWRAPPEROGL_API __declspec(dllexport)
#else
#define S3DWRAPPEROGL_API __declspec(dllimport)
#endif

// OutputMode values for OGL wrapper
// 0 = iZ3D dual-panel hardware (legacy default)
// 1 = Side-by-Side (L left, R right)
// 2 = Over/Under (L top, R bottom)
// 3 = Crosseyed (L right, R left)
// 4 = Anaglyph Red/Cyan (grayscale)
// 5 = Optimized Anaglyph (Dubois)
// 6 = Color Anaglyph
// 7 = Line Interleaved (passive 3D TVs / monitors with row-polarised filter)
// 8 = Column Interleaved (rarer column-polarised displays)
// 9 = Checkerboard (DLP-Link 3D projectors)
#define OGL_OUTPUT_IZ3D            0
#define OGL_OUTPUT_SBS             1
#define OGL_OUTPUT_OVERUNDER       2
#define OGL_OUTPUT_CROSSEYED       3
#define OGL_OUTPUT_ANAGLYPH        4
#define OGL_OUTPUT_OPT_ANAGLYPH    5
#define OGL_OUTPUT_COLOR_ANAGLYPH  6
#define OGL_OUTPUT_LINE_INTERLEAVED   7
#define OGL_OUTPUT_COLUMN_INTERLEAVED 8
#define OGL_OUTPUT_CHECKERBOARD       9

struct GlobalInfo
{                   
	UINT		Trace;
	UINT		EmulateQB; 
	UINT		RouterType;
	UINT		OutputMode;
	TCHAR		DriverDirectory[MAX_PATH];
	TCHAR		DriverFileName[MAX_PATH];
	TCHAR       ApplicationFileName[MAX_PATH];
	TCHAR		ApplicationName[MAX_PATH];
	TCHAR       LogFileName[MAX_PATH];
	TCHAR       LogFileDirectory[MAX_PATH];
	GlobalInfo()
	{
		ZeroMemory(this, sizeof(GlobalInfo));
	}
	void Initialize(HMODULE hModule);
};

extern HMODULE g_hDllInstance;
extern GlobalInfo gInfo;

#define PROFILES_VERSION	1
#define GINFO_ENABLED		(gInfo.RouterType != 2)
