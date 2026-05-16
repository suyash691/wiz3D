/*
 * Minimal Simulated Reality interface for DX9 weaving
 * Avoids full SDK dependency chain while providing just what we need
 */

#pragma once

#include <d3d9.h>

namespace SR {
    // Forward declare - we don't need to know the implementation
    class SRContext;
    class IDX9Weaver1;

    // Error codes from WeaverTypes.h
    enum WeaverErrorCode {
        WeaverSuccess = 0,
        WeaverTextureNotFound = 1,
        WeaverTextureFailedToLoad = 2,
        WeaverTextureUnknownPixelFormat = 3,
    };

    // Factory function - C++ mangled name in SimulatedRealityDirectX.lib
    // Returns WeaverSuccess (0) on success, non-zero error code on failure
    extern WeaverErrorCode CreateDX9Weaver(
        SRContext* context,
        IDirect3DDevice9* d3d9Device,
        HWND window,
        IDX9Weaver1** weaver
    );

    // Virtual interface - just declarations for method calls
    class IDX9Weaver1 {
    public:
        virtual ~IDX9Weaver1() = default;

        virtual void setInputViewTexture(IDirect3DTexture9* texture, int width, int height, D3DFORMAT format, bool isSRGB) = 0;
        virtual void setOutputSRGBWrite(bool sRGBWriteEnable) = 0;
        virtual void weave() = 0;
        virtual void setWindowHandle(HWND handle) = 0;
        virtual void destroy() = 0;
    };
}

