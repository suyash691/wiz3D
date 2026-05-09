/* NvDirectMode - shared GPU spoof identity
 *
 * These constants must match what NvApiProxy/NvApiProxy.cpp reports
 * (see kSpoofGpuName / kSpoofPciVendor / etc. there). The DXGI adapter
 * spoof in d3d11.dll's DXGIAdapterProxy and dxgi.dll's DXGIAdapterProxy
 * uses these values so games querying "what GPU is this?" via DXGI get
 * the same answer as games querying via NvAPI.
 *
 * Why we do this: many games (Tomb Raider 2013 in particular) check
 * IDXGIAdapter::GetDesc().VendorId to decide whether to load nvapi.dll
 * at all. On AMD/Intel hardware they see a non-NVIDIA vendor and skip
 * the entire NVIDIA stereo path. Spoofing the DXGI adapter as NVIDIA
 * makes them load nvapi.dll, which our NvApiProxy then handles to
 * report stereo as available.
 *
 * Identity: RTX 2080 Ti / driver 426.06 — last NVIDIA card and driver
 * to ship with official 3D Vision support.
 */

#pragma once

#include <windows.h>

namespace NvDirectMode
{
    // Spoofed GPU description string. UTF-8 here; NvApiProxy uses ANSI
    // and DXGI uses UTF-16 — converted at use site.
    constexpr const char*    kSpoofGpuName     = "NVIDIA GeForce RTX 2080 Ti";
    constexpr const wchar_t* kSpoofGpuNameW    = L"NVIDIA GeForce RTX 2080 Ti";

    // PCI identifiers — same values NvApiProxy reports. SubSysId is the
    // packed (subDevice << 16) | subVendor value DXGI exposes as a single
    // DWORD.
    constexpr UINT kSpoofPciVendor    = 0x10DE;        // NVIDIA Corporation
    constexpr UINT kSpoofPciDevice    = 0x1E07;        // RTX 2080 Ti (TU102)
    constexpr UINT kSpoofPciSubVendor = 0x10DE;
    constexpr UINT kSpoofPciSubDevice = 0x12FF;
    constexpr UINT kSpoofSubSysId     = (kSpoofPciSubDevice << 16) | kSpoofPciSubVendor;
    constexpr UINT kSpoofRevision     = 0xA1;
}
