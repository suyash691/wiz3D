/* NvDirectMode/dxgi - IDXGIAdapter / IDXGIAdapter1 vtable hot-patch
 *
 * Task #66 — bypass the system-d3d11.dll layout-sniff that crashes when
 * we wrap adapters returned from IDXGIFactory::EnumAdapters{,1}. Real
 * d3d11 walks adapter struct internals past the vtable during
 * D3D11CreateDeviceAndSwapChain; our DXGIAdapterProxy class has the
 * wrong layout → access violation.
 *
 * Solution: instead of returning a wrapped object, hot-patch the real
 * IDXGIAdapter / IDXGIAdapter1 vtable's GetDesc / GetDesc1 slots so
 * the spoof fires regardless of who returned the pointer (game OR
 * system d3d11). The underlying adapter struct layout is preserved →
 * d3d11's internal accesses work.
 *
 * Vtable patches are process-global (DLL .data is COW per-process so
 * we don't affect other processes). Idempotent — first call patches,
 * subsequent calls skip.
 *
 * Usage: DXGIFactoryProxy::WrapRealFactoryAsRequested() calls
 * InstallAdapterVtablePatch() with the real factory it just received,
 * which is used to enumerate one real adapter (released immediately)
 * to grab the IDXGIAdapter / IDXGIAdapter1 vtables.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>

namespace NvDirectMode
{
    // Patches IDXGIAdapter::GetDesc (slot 8) and IDXGIAdapter1::GetDesc1
    // (slot 10) so all callers receive the spoofed NVIDIA RTX 2080 Ti
    // identity. Returns true if patching succeeded (or was already done).
    bool InstallAdapterVtablePatch(IDXGIFactory* realFactory);
}
