/* NvDirectMode d3d10 - active-eye query bridge (mirror of d3d9/d3d11 versions) */

#pragma once

namespace NvDirectMode
{
    constexpr int kEyeRight = 1;
    constexpr int kEyeLeft  = 2;
    constexpr int kEyeMono  = 3;

    int GetActiveEye();

    // Stage 4 per-eye capture: register a callback with NvApiProxy that
    // fires every time the game changes Stereo_SetActiveEye. Used by
    // SwapChainProxy to capture the OLD eye's render before the new
    // eye's render starts overwriting the shadow.
    typedef void (*EyeChangeHandler)(int oldEye, int newEye);
    void RegisterEyeChangeHandler(EyeChangeHandler handler);
}
