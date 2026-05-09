/* NvDirectMode opengl32 - per-eye FBO state
 *
 * The two-FBO setup (color textures per eye + shared depth/stencil
 * renderbuffer) and the wglGetProcAddress-based hook strategy are adapted
 * from OGL-3DVision-Wrapper:
 *   https://github.com/Helifax/OpenGL3DVision
 *   Copyright (c) 2015 Octavian Mihai Vasilovici aka Helifax
 *   MIT License - see THIRD_PARTY_NOTICES.txt for full text.
 *
 * Conceptual changes vs the original:
 *   - The original wrapper performs ACTIVE stereoization (it analyses + injects
 *     vertex shaders to render the second eye from a single mono draw call).
 *     NvDirectMode does PASSIVE routing — the game already renders both eyes
 *     explicitly via NvAPI_Stereo_SetActiveEye, so we just route each draw to
 *     the correct FBO based on the active-eye state.
 *   - The original delivers stereo via GL/DX9 interop (wglDXLockObjectsNV) so
 *     NVIDIA's driver can present in 3D Vision. NvDirectMode just blits the
 *     active-eye FBO to the GL window backbuffer at swap time; downstream
 *     stereo composition (SR weaver / direct mode display) is a separate
 *     vertical not handled here.
 *   - The original uses an internal frame counter (1 ↔ 2) to drive eye
 *     selection. NvDirectMode reads NvAPI's active-eye via the
 *     Wiz3D_GetActiveEye bridge.
 */

#include "fbo_state.h"

#include <GL/gl.h>
#include "eye_state.h"

// OutputMode flag from dllmain
extern "C" int NvDM_OutputIsTopBottom();
extern "C" int NvDM_SwapEyes();

// ---------------------------------------------------------------------------
// GL constants and types we need that are NOT in <GL/gl.h>'s GL 1.1 set.
// We avoid pulling in <GL/glext.h> to keep this TU's includes minimal.
// Values come from the OpenGL spec / glext.h public.
// ---------------------------------------------------------------------------
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                    0x8D40
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_FRAMEBUFFER_BINDING            0x8CA6
#define GL_RENDERBUFFER                   0x8D41
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_DEPTH24_STENCIL8               0x88F0
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#endif

typedef void   (APIENTRY *PFN_glGenFramebuffers)(GLsizei n, GLuint* ids);
typedef void   (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei n, const GLuint* ids);
typedef void   (APIENTRY *PFN_glBindFramebuffer)(GLenum target, GLuint id);
typedef void   (APIENTRY *PFN_glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void   (APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb);
typedef void   (APIENTRY *PFN_glGenRenderbuffers)(GLsizei n, GLuint* ids);
typedef void   (APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei n, const GLuint* ids);
typedef void   (APIENTRY *PFN_glBindRenderbuffer)(GLenum target, GLuint id);
typedef void   (APIENTRY *PFN_glRenderbufferStorage)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum target);
typedef void   (APIENTRY *PFN_glBlitFramebuffer)(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter);

// ---------------------------------------------------------------------------
// dllmain owns g_hRealOpenGL32 + the orig_wglGetProcAddress entry. We resolve
// extension fns via that. Forward-declared here.
// ---------------------------------------------------------------------------
extern "C" HMODULE g_hRealOpenGL32;
typedef PROC (WINAPI* pfnWglGetProcAddress)(LPCSTR);
extern "C" pfnWglGetProcAddress orig_wglGetProcAddress;

namespace NvDirectMode
{

namespace
{
    // Resolved GL extension entry points
    PFN_glGenFramebuffers          p_glGenFramebuffers          = nullptr;
    PFN_glDeleteFramebuffers       p_glDeleteFramebuffers       = nullptr;
    PFN_glBindFramebuffer          p_glBindFramebuffer          = nullptr;
    PFN_glFramebufferTexture2D     p_glFramebufferTexture2D     = nullptr;
    PFN_glFramebufferRenderbuffer  p_glFramebufferRenderbuffer  = nullptr;
    PFN_glGenRenderbuffers         p_glGenRenderbuffers         = nullptr;
    PFN_glDeleteRenderbuffers      p_glDeleteRenderbuffers      = nullptr;
    PFN_glBindRenderbuffer         p_glBindRenderbuffer         = nullptr;
    PFN_glRenderbufferStorage      p_glRenderbufferStorage      = nullptr;
    PFN_glCheckFramebufferStatus   p_glCheckFramebufferStatus   = nullptr;
    PFN_glBlitFramebuffer          p_glBlitFramebuffer          = nullptr;

    // GL 1.1 entry points (resolved from g_hRealOpenGL32 directly)
    typedef void (APIENTRY *PFN_glGenTextures)(GLsizei n, GLuint* textures);
    typedef void (APIENTRY *PFN_glBindTexture)(GLenum target, GLuint texture);
    typedef void (APIENTRY *PFN_glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
    typedef void (APIENTRY *PFN_glTexParameteri)(GLenum target, GLenum pname, GLint param);
    typedef void (APIENTRY *PFN_glDeleteTextures)(GLsizei n, const GLuint* textures);
    PFN_glGenTextures              p_glGenTextures              = nullptr;
    PFN_glBindTexture              p_glBindTexture              = nullptr;
    PFN_glTexImage2D               p_glTexImage2D               = nullptr;
    PFN_glTexParameteri            p_glTexParameteri            = nullptr;
    PFN_glDeleteTextures           p_glDeleteTextures           = nullptr;

    // Per-eye FBO state
    struct EyeFbos
    {
        GLuint fboLeft         = 0;
        GLuint fboRight        = 0;
        GLuint texLeft         = 0;
        GLuint texRight        = 0;
        GLuint depthStencilRb  = 0;
        int    width           = 0;
        int    height          = 0;
        bool   ready           = false;
    };
    EyeFbos g_fbos;

    template<typename T>
    bool ResolveExt(T& dst, const char* name)
    {
        if (!orig_wglGetProcAddress) return false;
        dst = (T)orig_wglGetProcAddress(name);
        return dst != nullptr;
    }
}

bool ResolveFboExtensions()
{
    if (!g_hRealOpenGL32) return false;

    // GL 1.1 fns: pull directly from opengl32.dll
    p_glGenTextures   = (PFN_glGenTextures)  GetProcAddress(g_hRealOpenGL32, "glGenTextures");
    p_glBindTexture   = (PFN_glBindTexture)  GetProcAddress(g_hRealOpenGL32, "glBindTexture");
    p_glTexImage2D    = (PFN_glTexImage2D)   GetProcAddress(g_hRealOpenGL32, "glTexImage2D");
    p_glTexParameteri = (PFN_glTexParameteri)GetProcAddress(g_hRealOpenGL32, "glTexParameteri");
    p_glDeleteTextures= (PFN_glDeleteTextures)GetProcAddress(g_hRealOpenGL32, "glDeleteTextures");

    // FBO + blit: GL 3.0 extensions, pulled via orig_wglGetProcAddress
    bool ok = true;
    ok &= ResolveExt(p_glGenFramebuffers,         "glGenFramebuffers");
    ok &= ResolveExt(p_glDeleteFramebuffers,      "glDeleteFramebuffers");
    ok &= ResolveExt(p_glBindFramebuffer,         "glBindFramebuffer");
    ok &= ResolveExt(p_glFramebufferTexture2D,    "glFramebufferTexture2D");
    ok &= ResolveExt(p_glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    ok &= ResolveExt(p_glGenRenderbuffers,        "glGenRenderbuffers");
    ok &= ResolveExt(p_glDeleteRenderbuffers,     "glDeleteRenderbuffers");
    ok &= ResolveExt(p_glBindRenderbuffer,        "glBindRenderbuffer");
    ok &= ResolveExt(p_glRenderbufferStorage,     "glRenderbufferStorage");
    ok &= ResolveExt(p_glCheckFramebufferStatus,  "glCheckFramebufferStatus");
    ok &= ResolveExt(p_glBlitFramebuffer,         "glBlitFramebuffer");

    // Some pre-3.0 contexts may only expose GL_EXT_framebuffer_object — try the
    // EXT-suffixed names for fns we couldn't resolve. Both forms are ABI-compatible.
    if (!p_glGenFramebuffers)         ResolveExt(p_glGenFramebuffers,         "glGenFramebuffersEXT");
    if (!p_glDeleteFramebuffers)      ResolveExt(p_glDeleteFramebuffers,      "glDeleteFramebuffersEXT");
    if (!p_glBindFramebuffer)         ResolveExt(p_glBindFramebuffer,         "glBindFramebufferEXT");
    if (!p_glFramebufferTexture2D)    ResolveExt(p_glFramebufferTexture2D,    "glFramebufferTexture2DEXT");
    if (!p_glFramebufferRenderbuffer) ResolveExt(p_glFramebufferRenderbuffer, "glFramebufferRenderbufferEXT");
    if (!p_glGenRenderbuffers)        ResolveExt(p_glGenRenderbuffers,        "glGenRenderbuffersEXT");
    if (!p_glDeleteRenderbuffers)     ResolveExt(p_glDeleteRenderbuffers,     "glDeleteRenderbuffersEXT");
    if (!p_glBindRenderbuffer)        ResolveExt(p_glBindRenderbuffer,        "glBindRenderbufferEXT");
    if (!p_glRenderbufferStorage)     ResolveExt(p_glRenderbufferStorage,     "glRenderbufferStorageEXT");
    if (!p_glCheckFramebufferStatus)  ResolveExt(p_glCheckFramebufferStatus,  "glCheckFramebufferStatusEXT");

    return p_glGenFramebuffers      && p_glBindFramebuffer        &&
           p_glFramebufferTexture2D && p_glFramebufferRenderbuffer &&
           p_glGenRenderbuffers     && p_glRenderbufferStorage     &&
           p_glBlitFramebuffer      && p_glGenTextures             &&
           p_glBindTexture          && p_glTexImage2D              &&
           p_glTexParameteri;
}

bool EyeFbosCreate(int width, int height)
{
    if (!ResolveFboExtensions()) return false;
    if (width <= 0 || height <= 0) return false;
    if (g_fbos.ready && g_fbos.width == width && g_fbos.height == height) return true;
    EyeFbosDestroy();

    // Color textures (one per eye)
    p_glGenTextures(1, &g_fbos.texLeft);
    p_glBindTexture(GL_TEXTURE_2D, g_fbos.texLeft);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    p_glGenTextures(1, &g_fbos.texRight);
    p_glBindTexture(GL_TEXTURE_2D, g_fbos.texRight);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glBindTexture(GL_TEXTURE_2D, 0);

    // Shared depth/stencil renderbuffer
    p_glGenRenderbuffers(1, &g_fbos.depthStencilRb);
    p_glBindRenderbuffer(GL_RENDERBUFFER, g_fbos.depthStencilRb);
    p_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    p_glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // FBO_left
    p_glGenFramebuffers(1, &g_fbos.fboLeft);
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbos.fboLeft);
    p_glFramebufferTexture2D   (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        GL_TEXTURE_2D, g_fbos.texLeft, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_fbos.depthStencilRb);
    GLenum sl = p_glCheckFramebufferStatus ? p_glCheckFramebufferStatus(GL_FRAMEBUFFER) : GL_FRAMEBUFFER_COMPLETE;

    // FBO_right (shares depth/stencil with left)
    p_glGenFramebuffers(1, &g_fbos.fboRight);
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbos.fboRight);
    p_glFramebufferTexture2D   (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        GL_TEXTURE_2D, g_fbos.texRight, 0);
    p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g_fbos.depthStencilRb);
    GLenum sr = p_glCheckFramebufferStatus ? p_glCheckFramebufferStatus(GL_FRAMEBUFFER) : GL_FRAMEBUFFER_COMPLETE;

    // Restore default binding so we don't leave game state altered
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    g_fbos.width  = width;
    g_fbos.height = height;
    g_fbos.ready  = (sl == GL_FRAMEBUFFER_COMPLETE) && (sr == GL_FRAMEBUFFER_COMPLETE);
    return g_fbos.ready;
}

void EyeFbosDestroy()
{
    if (p_glDeleteFramebuffers)
    {
        if (g_fbos.fboLeft)  p_glDeleteFramebuffers(1, &g_fbos.fboLeft);
        if (g_fbos.fboRight) p_glDeleteFramebuffers(1, &g_fbos.fboRight);
    }
    if (p_glDeleteRenderbuffers && g_fbos.depthStencilRb)
        p_glDeleteRenderbuffers(1, &g_fbos.depthStencilRb);
    if (p_glDeleteTextures)
    {
        if (g_fbos.texLeft)  p_glDeleteTextures(1, &g_fbos.texLeft);
        if (g_fbos.texRight) p_glDeleteTextures(1, &g_fbos.texRight);
    }
    g_fbos = EyeFbos{};
}

void EyeFbosBindForActiveEye(GLenum target)
{
    if (!g_fbos.ready || !p_glBindFramebuffer)
    {
        if (p_glBindFramebuffer) p_glBindFramebuffer(target, 0);
        return;
    }
    const int eye = GetActiveEye();
    GLuint id = (eye == kEyeRight) ? g_fbos.fboRight
              : (eye == kEyeLeft)  ? g_fbos.fboLeft
              :                       0;          // MONO -> real default
    p_glBindFramebuffer(target, id);
}

void EyeFbosBlitToDefault()
{
    if (!g_fbos.ready || !p_glBindFramebuffer || !p_glBlitFramebuffer) return;

    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    const int W = g_fbos.width;
    const int H = g_fbos.height;
    const bool topBottom = NvDM_OutputIsTopBottom() != 0;
    const bool swap      = NvDM_SwapEyes() != 0;

    // SwapEyes flips which captured eye lands in which display half — game
    // still rendered LEFT-eye-content into fboLeft (live FBO redirect on
    // glBindFramebuffer(0)), but at composite time we put RIGHT content
    // in the LEFT slot and vice versa.
    GLuint leftSlotFbo  = swap ? g_fbos.fboRight : g_fbos.fboLeft;
    GLuint rightSlotFbo = swap ? g_fbos.fboLeft  : g_fbos.fboRight;

    if (topBottom)
    {
        // Half T-B: "left" eye (logical) -> top half, "right" -> bottom half
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, leftSlotFbo);
        p_glBlitFramebuffer(0, 0, W, H,
                            0, H / 2, W, H,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, rightSlotFbo);
        p_glBlitFramebuffer(0, 0, W, H,
                            0, 0, W, H / 2,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    else
    {
        // Half SBS (default): "left" -> left half, "right" -> right half
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, leftSlotFbo);
        p_glBlitFramebuffer(0, 0, W, H,
                            0, 0, W / 2, H,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, rightSlotFbo);
        p_glBlitFramebuffer(0, 0, W, H,
                            W / 2, 0, W, H,
                            GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // Re-bind our LEFT FBO for the next frame (game rebinds 0 first thing
    // anyway, which our hook redirects to the active eye).
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbos.fboLeft);
}

bool IsOurFboId(GLuint id)
{
    return g_fbos.ready && (id == g_fbos.fboLeft || id == g_fbos.fboRight);
}

int  GetFboWidth()  { return g_fbos.width; }
int  GetFboHeight() { return g_fbos.height; }
bool IsFboReady()   { return g_fbos.ready; }

} // namespace NvDirectMode
