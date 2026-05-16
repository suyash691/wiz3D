/*
 * Simulated Reality OpenGL weaver integration for S3DWrapperOGL (mode 9).
 * See SRWeaveOGL.h for the public API contract.
 */

#include "stdafx.h"
#include "SRWeaveOGL.h"

#include <GL/gl.h>

// The SR header chain pulls in opencv2/core/types.hpp which uses
// reinterpret_cast between related classes (cv::Matx <-> cv::Scalar_).
// Common.props promotes C4946 to an error; harmless here because we
// never touch those types ourselves, so suppress just for this TU.
#pragma warning(disable: 4946)

// Simulated Reality SDK. Headers are identical across the two SDK versions we vendor:
//   x64:   lib/Simulated Reality/LeiaSR-SDK-1.36.2-win64/include
//   Win32: lib/Simulated Reality/simulatedreality-1.34.10-win32-Release/include
// Both export the same IGLWeaver1 / CreateGLWeaver surface; the include
// path is set per-arch in S3DWrapperOGL.vcxproj.
#include "sr/management/srcontext.h"
#include "sr/weaver/glweaver.h"
#include "sr/utility/exception.h"

// OpenCV-free link stub. SR's Exception class has a cv::String message
// member, so the compiler emits a reference to cv::String::deallocate
// anywhere an SR exception might be destroyed during stack unwind.
// We never actually construct cv::String at runtime — only the SR runtime
// throws SR exceptions, and our catch(...) doesn't bind the typed exception.
// This empty stub satisfies the linker without pulling in the ~10 MB of
// OpenCV static init that opencv_world343.lib would drag in.  Same trick
// NvDirectMode/d3d11/SwapChainProxy.cpp uses for the DX11 SR weave path.
void cv::String::deallocate() {}

// GL FBO entry points — loaded via wglGetProcAddress so we don't introduce
// a static dep on a specific GL header version. Same pattern S3DWrapperOGL
// already uses for the WGL_ARB extensions.
typedef void   (APIENTRY *PFN_GenFramebuffers)(GLsizei n, GLuint* framebuffers);
typedef void   (APIENTRY *PFN_BindFramebuffer)(GLenum target, GLuint framebuffer);
typedef void   (APIENTRY *PFN_DeleteFramebuffers)(GLsizei n, const GLuint* framebuffers);
typedef void   (APIENTRY *PFN_FramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (APIENTRY *PFN_CheckFramebufferStatus)(GLenum target);

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER          0x8D40
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_SRGB8_ALPHA8         0x8C43
#endif

struct SRWeaveOGLContext {
    SR::SRContext*  srContext;
    SR::IGLWeaver1* weaver;
    GLuint          sbsTexture;       // size = (viewWidth*2) x viewHeight, RGBA8/sRGB
    GLuint          sbsFramebuffer;
    unsigned int    viewWidth;        // per-eye width (sbs texture is 2x this)
    unsigned int    viewHeight;
    bool            fallback;         // sticky; once true we never retry SR this session

    PFN_GenFramebuffers        glGenFramebuffers;
    PFN_BindFramebuffer        glBindFramebuffer;
    PFN_DeleteFramebuffers     glDeleteFramebuffers;
    PFN_FramebufferTexture2D   glFramebufferTexture2D;
    PFN_CheckFramebufferStatus glCheckFramebufferStatus;
};

// Try the core 3.0 names first, fall back to the EXT 1.5+ variants. Returns
// true if a full set was resolved (some IHVs only export one suffix).
static bool LoadFBOEntryPoints(SRWeaveOGLContext* ctx)
{
    ctx->glGenFramebuffers        = (PFN_GenFramebuffers)       wglGetProcAddress("glGenFramebuffers");
    ctx->glBindFramebuffer        = (PFN_BindFramebuffer)       wglGetProcAddress("glBindFramebuffer");
    ctx->glDeleteFramebuffers     = (PFN_DeleteFramebuffers)    wglGetProcAddress("glDeleteFramebuffers");
    ctx->glFramebufferTexture2D   = (PFN_FramebufferTexture2D)  wglGetProcAddress("glFramebufferTexture2D");
    ctx->glCheckFramebufferStatus = (PFN_CheckFramebufferStatus)wglGetProcAddress("glCheckFramebufferStatus");

    bool haveCore = ctx->glGenFramebuffers && ctx->glBindFramebuffer &&
                    ctx->glDeleteFramebuffers && ctx->glFramebufferTexture2D &&
                    ctx->glCheckFramebufferStatus;
    if (haveCore) return true;

    ctx->glGenFramebuffers        = (PFN_GenFramebuffers)       wglGetProcAddress("glGenFramebuffersEXT");
    ctx->glBindFramebuffer        = (PFN_BindFramebuffer)       wglGetProcAddress("glBindFramebufferEXT");
    ctx->glDeleteFramebuffers     = (PFN_DeleteFramebuffers)    wglGetProcAddress("glDeleteFramebuffersEXT");
    ctx->glFramebufferTexture2D   = (PFN_FramebufferTexture2D)  wglGetProcAddress("glFramebufferTexture2DEXT");
    ctx->glCheckFramebufferStatus = (PFN_CheckFramebufferStatus)wglGetProcAddress("glCheckFramebufferStatusEXT");

    return ctx->glGenFramebuffers && ctx->glBindFramebuffer &&
           ctx->glDeleteFramebuffers && ctx->glFramebufferTexture2D &&
           ctx->glCheckFramebufferStatus;
}

bool SRWeaveOGL_Initialize(SRWeaveOGLContext** outCtx, HWND hWnd,
                           unsigned int viewWidth, unsigned int viewHeight)
{
    *outCtx = nullptr;
    SRWeaveOGLContext* ctx = new SRWeaveOGLContext();
    ctx->srContext = nullptr;
    ctx->weaver = nullptr;
    ctx->sbsTexture = 0;
    ctx->sbsFramebuffer = 0;
    ctx->viewWidth = viewWidth;
    ctx->viewHeight = viewHeight;
    ctx->fallback = false;

    if (!LoadFBOEntryPoints(ctx)) {
        OutputDebugStringA("[SRWeaveOGL] FBO entry points unavailable - context too old. Falling back to plain SBS.\n");
        ctx->fallback = true;
        *outCtx = ctx;
        return false;
    }

    // SBS texture: 2*viewWidth wide, viewHeight tall, plain RGBA8 (linear).
    // Earlier sRGB-encoded format darkened the final image: the wrapper's
    // fixed-function copy from per-eye PBuffer textures to this SBS texture
    // doesn't enable GL_FRAMEBUFFER_SRGB on write, so display-encoded source
    // bytes were stored as-is; then the weaver's sample of a GL_SRGB8_ALPHA8
    // texture sRGB-decodes them (treating 0.5 as ~0.21 linear), and there's
    // no setOutputSRGBWrite on IGLWeaver1 to re-encode. End result: half
    // brightness. Plain RGBA8 keeps values byte-identical through the pipe.
    glGenTextures(1, &ctx->sbsTexture);
    glBindTexture(GL_TEXTURE_2D, ctx->sbsTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewWidth * 2, viewHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glBindTexture(GL_TEXTURE_2D, 0);

    ctx->glGenFramebuffers(1, &ctx->sbsFramebuffer);
    ctx->glBindFramebuffer(GL_FRAMEBUFFER, ctx->sbsFramebuffer);
    ctx->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->sbsTexture, 0);
    GLenum fbStatus = ctx->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    ctx->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
        OutputDebugStringA("[SRWeaveOGL] SBS framebuffer incomplete. Falling back to plain SBS.\n");
        if (ctx->sbsFramebuffer) ctx->glDeleteFramebuffers(1, &ctx->sbsFramebuffer);
        if (ctx->sbsTexture) glDeleteTextures(1, &ctx->sbsTexture);
        ctx->sbsFramebuffer = 0;
        ctx->sbsTexture = 0;
        ctx->fallback = true;
        *outCtx = ctx;
        return false;
    }

    // SR context. SRContext::create() throws ServerNotAvailableException when
    // the SR Service isn't running (or DELAYLOAD couldn't bind the DLL).
    try {
        ctx->srContext = SR::SRContext::create();
    }
    catch (...) {
        OutputDebugStringA("[SRWeaveOGL] Simulated Reality runtime unavailable. Falling back to plain SBS.\n");
        ctx->glDeleteFramebuffers(1, &ctx->sbsFramebuffer);
        glDeleteTextures(1, &ctx->sbsTexture);
        ctx->sbsFramebuffer = 0;
        ctx->sbsTexture = 0;
        ctx->fallback = true;
        *outCtx = ctx;
        return false;
    }

    WeaverErrorCode rc = SR::CreateGLWeaver(*ctx->srContext, hWnd, &ctx->weaver);
    if (rc != WeaverSuccess) {
        OutputDebugStringA("[SRWeaveOGL] CreateGLWeaver failed. Falling back to plain SBS.\n");
        SR::SRContext::deleteSRContext(ctx->srContext);
        ctx->srContext = nullptr;
        ctx->glDeleteFramebuffers(1, &ctx->sbsFramebuffer);
        glDeleteTextures(1, &ctx->sbsTexture);
        ctx->sbsFramebuffer = 0;
        ctx->sbsTexture = 0;
        ctx->fallback = true;
        *outCtx = ctx;
        return false;
    }

    // setInputViewTexture takes the TOTAL SBS texture dimensions (2*viewWidth,
    // viewHeight), not per-eye. Same contract as the DX11/DX9 weave paths
    // (see NvDirectMode/d3d11 SwapChainProxy.cpp passing m_srSBSW = logicalW*2).
    // Passing per-eye dims here is what produced the black-screen RtCW test:
    // SR thinks the texture is 1x but the actual layout is 2x, so the weaver
    // splits and reads garbage.
    ctx->weaver->setInputViewTexture(ctx->sbsTexture, viewWidth * 2, viewHeight, GL_RGBA8);

    // Per the SR SDK contract, initialize() runs AFTER weaver creation.
    ctx->srContext->initialize();

    *outCtx = ctx;
    return true;
}

// Set while we're inside the known-AV-prone SR runtime cleanup calls so the
// proxy's VEH can choose to skip dump-writing when the AV is one we expect
// and have an inner SEH handler for. Defined here, exported via the .def
// alongside HookOGL so the proxy GetProcAddress's it at load time.
volatile LONG g_suppressCrashDump = 0;

extern "C" __declspec(dllexport) BOOL WINAPI ShouldSuppressCrashDump()
{
    return InterlockedCompareExchange(&g_suppressCrashDump, 0, 0) != 0;
}

void SRWeaveOGL_Cleanup(SRWeaveOGLContext** ctxPtr)
{
    if (!ctxPtr || !*ctxPtr) return;
    SRWeaveOGLContext* ctx = *ctxPtr;

    // Process-exit ordering: the wrapper's Renderer destructors run from
    // C++ static dtors (g_RendererList), which fire during _cexit. At that
    // point the SR runtime's own statics may already have torn down even
    // though the DLLs are still loaded — the crash dump traced through
    // glog.dll into SR core, suggesting SR's logging singleton was already
    // gone. The first defence is the GetModuleHandle check below (catches
    // the unloaded-DLL case the delay-load helper would otherwise trip on);
    // the second is SEH-wrapping the SR calls so anything else corrupted
    // inside the runtime can't take down the process during exit.
#ifdef _WIN64
    HMODULE hSRCore = GetModuleHandleW(L"SimulatedRealityCore.dll");
    HMODULE hSRGL   = GetModuleHandleW(L"SimulatedRealityOpenGL.dll");
#else
    HMODULE hSRCore = GetModuleHandleW(L"SimulatedRealityCore32.dll");
    HMODULE hSRGL   = GetModuleHandleW(L"SimulatedRealityOpenGL32.dll");
#endif
    bool srRuntimeAlive = (hSRCore != NULL && hSRGL != NULL);

    if (srRuntimeAlive)
    {
        // Tell the proxy's crash-dump VEH that any AV from here is expected
        // and has an inner SEH handler — it should skip the dump file write.
        InterlockedExchange(&g_suppressCrashDump, 1);
        if (ctx->weaver) {
            __try {
                ctx->weaver->destroy();
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                        ? EXCEPTION_EXECUTE_HANDLER
                        : EXCEPTION_CONTINUE_SEARCH) {
                OutputDebugStringA("[SRWeaveOGL] weaver->destroy() raised AV - swallowing for clean process exit.\n");
            }
            ctx->weaver = nullptr;
        }
        if (ctx->srContext) {
            __try {
                SR::SRContext::deleteSRContext(ctx->srContext);
            }
            __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                        ? EXCEPTION_EXECUTE_HANDLER
                        : EXCEPTION_CONTINUE_SEARCH) {
                OutputDebugStringA("[SRWeaveOGL] deleteSRContext raised AV - swallowing for clean process exit.\n");
            }
            ctx->srContext = nullptr;
        }
        InterlockedExchange(&g_suppressCrashDump, 0);
    }
    if (ctx->sbsFramebuffer && ctx->glDeleteFramebuffers) {
        ctx->glDeleteFramebuffers(1, &ctx->sbsFramebuffer);
        ctx->sbsFramebuffer = 0;
    }
    if (ctx->sbsTexture) {
        glDeleteTextures(1, &ctx->sbsTexture);
        ctx->sbsTexture = 0;
    }
    delete ctx;
    *ctxPtr = nullptr;
}

// Render a fullscreen textured quad using fixed-function GL (immediate mode)
// — keeps us out of the wrapper's bound GLSL program and matches the existing
// renderer's drawing style. texCoordMax {X,Y} = fraction of the source
// texture that holds live image (handles the wrapper's pow2-padded textures).
// Caller has bound the target FBO and viewport.
static void DrawFullscreenTexturedQuad(GLuint textureId, float texCoordMaxX, float texCoordMaxY)
{
    glBindTexture(GL_TEXTURE_2D, textureId);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f,         0.0f);          glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(texCoordMaxX, 0.0f);          glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(texCoordMaxX, texCoordMaxY);  glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f,         texCoordMaxY);  glVertex2f(-1.0f,  1.0f);
    glEnd();
}

bool SRWeaveOGL_Render(SRWeaveOGLContext* ctx,
                       unsigned int leftTexId, unsigned int rightTexId,
                       float texCoordX, float texCoordY,
                       unsigned int windowWidth, unsigned int windowHeight)
{
    if (!ctx || ctx->fallback || !ctx->weaver) return false;

    // Step 1: copy left + right eye textures into the two halves of the
    // SBS framebuffer texture. We unbind the wrapper's GLSL program so
    // fixed-function texturing applies — pixel-perfect copy, no gamma drift.
    GLint savedProgram = 0;
    glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &savedProgram);

    typedef void (APIENTRY *PFN_UseProgram)(GLuint program);
    typedef void (APIENTRY *PFN_ActiveTexture)(GLenum texture);
    static PFN_UseProgram   useProgram   = nullptr;
    static PFN_ActiveTexture activeTexture = nullptr;
    if (!useProgram)   useProgram   = (PFN_UseProgram)  wglGetProcAddress("glUseProgram");
    if (!activeTexture) activeTexture = (PFN_ActiveTexture)wglGetProcAddress("glActiveTexture");
    if (useProgram) useProgram(0);

    // Caller leaves active texture unit at GL_TEXTURE1 (after binding the two
    // PBuffer eye textures to units 0+1). Fixed-function glBegin/glTexCoord2f
    // samples from unit 0 by default, so force unit 0 before draw to avoid a
    // unit-mismatch that produces blank quads.
    if (activeTexture) activeTexture(0x84C0 /* GL_TEXTURE0 */);

    ctx->glBindFramebuffer(GL_FRAMEBUFFER, ctx->sbsFramebuffer);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glViewport(0, 0, (GLsizei)ctx->viewWidth, (GLsizei)ctx->viewHeight);
    DrawFullscreenTexturedQuad(leftTexId, texCoordX, texCoordY);

    glViewport((GLsizei)ctx->viewWidth, 0, (GLsizei)ctx->viewWidth, (GLsizei)ctx->viewHeight);
    DrawFullscreenTexturedQuad(rightTexId, texCoordX, texCoordY);

    glBindTexture(GL_TEXTURE_2D, 0);
    ctx->glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Step 2: full-window viewport, hand off to the SR runtime.
    glViewport(0, 0, (GLsizei)windowWidth, (GLsizei)windowHeight);

    bool ok = true;
    try {
        // weave() is inherited from IWeaverBase1; the chain is brought in by
        // sr/weaver/glweaver.h's includes.
        ctx->weaver->weave();
    }
    catch (...) {
        OutputDebugStringA("[SRWeaveOGL] weave() raised an exception - disabling SR for this session.\n");
        ctx->fallback = true;
        ok = false;
    }

    // Restore previously-bound shader program (the wrapper's compose shader,
    // unused in mode 9 but other code may depend on it being set).
    if (useProgram && savedProgram) useProgram((GLuint)savedProgram);

    return ok;
}

bool SRWeaveOGL_IsFallback(SRWeaveOGLContext* ctx)
{
    return !ctx || ctx->fallback;
}
