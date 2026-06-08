# `lib/` — orphan vendor bits

This tree holds vendored third-party code that doesn't fit the standard
`ThirdPartyLibs/` shape: these are narrow-purpose, single-set-of-consumers, 
frozen upstream, "best we can find." Adding to it should be deliberate — most 
new vendored libs belong in `ThirdPartyLibs/` instead.

---

## How this differs from `ThirdPartyLibs/`

The two trees use the same word ("vendor drop") but mean different things.

**`ThirdPartyLibs/`** is for libs that plug into the standard build plumbing:

- Standard layout: `<lib>/Include/`, `<lib>/lib/<arch>/<config>/`
- A `$(XxxLib)` name property in `ThirdPartyLibs.props` picks the right
  arch/config-specific filename so consumers don't hand-type it
- Include and lib paths are added **globally** to every project via the
  shared `ThirdPartyLibs.props`
- Consumed by many projects across the solution
- Generally a recognizable, still-maintained upstream — vcpkg-port-shaped

Current `ThirdPartyLibs/`: MinHook, SR-Lib, zlib, Xerces-C, TinyXML 2.6.2,
libsquish, DevIL, DirectX SDK (June 2010), Boost.

**`lib/`** is for the misfits:

- No standard layout — each entry's structure is whatever the original
  source shipped
- No global `.props` plumbing — projects that need an entry add per-project
  `AdditionalIncludeDirectories` / `AdditionalLibraryDirectories`
- Typically consumed by **one or two** specific projects
- Upstream is usually dormant or gone; the drop is "best we can find" and
  will not be version-bumped
- Putting something here is itself a signal: *do not modernize this*

The rule of thumb: if the lib plugs cleanly into the
`$(XxxLib)`/`_Arch86`/`_Config` convention and has multiple consumers, it
goes in `ThirdPartyLibs/`. If it's narrow, weird, or frozen, it stays here.

---

## The XML routing decision (`ticpp_2.5.3` + `LlamaXML/LlamaXML_shim`)

This is the most recent addition to `lib/`, and the reason this README
exists. The short version: the codebase parses XML through two parallel
paths, and trying to collapse them into one creates more risk than it
removes.

### The two paths

**Path A — direct TinyXML** (~17 projects). Files like
`S3DAPI/ReadData.cpp` and the OutputMethods `Output_dx9.cpp` files
`#include <tinyxml.h>` and use `TiXmlDocument`/`TiXmlElement` directly.
These consume **`ThirdPartyLibs/TinyXML_v2.6.2/`** — a clean vendor drop
in the standard layout, native `QueryBoolAttribute`, native
`TiXmlDocument(std::string)` ctor under `TIXML_USE_STL`. Standard
`$(TinyXMLLib)` name property, no patches.

**Path B — LlamaXML shim** (3 projects: S3DWrapper9, S3DWrapper10,
DX9GenerateCodeFromDump). The original code linked against the GPL-2
LlamaXML library for SAX-style streaming XML in the command-buffer dump
path. The GPL license is incompatible with this project, so
`lib/LlamaXML/LlamaXML_shim/` is an LGPL 2.1 **API-compatible
reimplementation** of LlamaXML — same public API surface (`XMLReader`,
`XMLWriter`, `FileInputStream`, `FileOutputStream`, `TextEncoding`,
`XMLException` in `namespace LlamaXML`) backed by ticpp internally so the
existing call sites don't need refactoring.

### Why ticpp 2.5.3 lives here, not in `ThirdPartyLibs/`

ticpp is "TinyXML++" — a C++ wrapper around TinyXML (the underlying
TinyXML is bundled inside the ticpp drop, not separate). The shim's
implementation files are the only consumers; nothing in the codebase uses
ticpp's `ticpp::Element` wrapper API directly. ticpp upstream
(wxFormBuilder/ticpp) has been dormant for years and 2.5.3 is the latest
available. Single narrow consumer + frozen upstream = exactly the `lib/`
profile, not the `ThirdPartyLibs/` profile.

The drop here was built from the wxFormBuilder/ticpp source with CMake
and `TIXML_USE_TICPP` on (which auto-defines `TIXML_USE_STL`); both
release and debug variants are named `ticpp.lib` and distinguished by
their parent directory.

### Why the two libs coexist instead of one winning

A natural instinct is "pick one and route everything through it." Both
collapse paths have a sharp edge:

- **Force everything onto ticpp 2.5.3:** ticpp bundles an older TinyXML
  2.5.3 (predates `QueryBoolAttribute`, added in TinyXML 2.6.0). Path A's
  17 projects use `QueryBoolAttribute` at multiple call sites — moving
  them to ticpp's bundled headers requires either patching the vendor
  drop to backport the method (modifying frozen vendor code, brittle) or
  rewriting every call site to use `Attribute()` + manual parsing.
- **Force everything onto TinyXML 2.6.2:** the shim's `ticpp.lib` was
  built against the 2.5.3 headers with `TIXML_USE_TICPP` on, which
  changes `TiXmlBase` to derive from `TiCppRC`. Compiling shim consumers
  against 2.6.2 headers (which don't have that base) is a silent
  `sizeof(TiXmlBase)` ABI mismatch — corruption with no compiler error.

Keeping the libs parallel — TinyXML 2.6.2 for path-A projects, ticpp 2.5.3
for path-B projects — avoids both problems. The cost is per-project
wiring (no global include path for either) and the rule that a single
translation unit must never see both `tinyxml.h` files on its search
path. The bucket separation makes that easy to enforce: a project is
either bucket 1 or bucket 2, never both.

- **Recommendation:** Remove _all_ XML parsing libraries in favor of a 
modern version of TinyXML-2 with a single, unified API. Will require
refactoring all call sites but will be more maintainable long-term. 

### One subtlety: the file `tinyxml.h` exists twice in the tree

- `ThirdPartyLibs/TinyXML_v2.6.2/Include/tinyxml.h` — the modern one,
  with native `QueryBoolAttribute`
- `lib/ticpp_2.5.3/Include/tinyxml.h` — the older one bundled inside
  ticpp, no `QueryBoolAttribute`

This is intentional. The two are NOT interchangeable. Bucket 1 projects
must only see the first; bucket 2 projects must only see the second.
That's why `ThirdPartyLibs.props` puts **neither** on the global include
path — each project opts in to exactly one per-project.

---

## Current contents

| Entry | Purpose | Consumers |
|---|---|---|
| `DXUT/` | DirectX UT framework, retro D3D7/9/10/11 sample helpers | DTest, TestsPack |
| `DbgHelp/` | Microsoft DbgHelp headers used by the minidump path. Cleanup pending — eventual fix is to call the system `dbghelp.dll` instead | `S3DAPI/MiniDump.cpp` |
| `LlamaXML/LlamaXML_shim/` | LGPL 2.1 API-compatible reimplementation of GPL LlamaXML, backed by ticpp. See the XML routing section above | S3DWrapper9, S3DWrapper10, DX9GenerateCodeFromDump |
| `d3d10/` | Misnamed — actually the WDK User-Mode Driver DDI headers for D3D9/10/11+DXGI. Load-bearing, do not move to `ThirdPartyLibs/` | DX10/11 wrapper paths |
| `d3d7/` | Vendored DX7 headers. Win10 SDK's `d3d.h` is gated `<0x0800` and becomes empty when `d3d9.h` is in the same TU, so the vendored copy is necessary | S3DWrapper7 |
| `d3d8/` | Vendored DX8.1 headers + import libs. Win10 SDK has zero DX8 content | S3DWrapper8 |
| `gl/` | OpenGL extension headers | S3DWrapperOGL |
| `madCHook/` | API shim layer over MinHook, exposing the historical MadCHook API to existing call sites | S3DAPI, S3DDriver bits |
| `python27/` | Embedded Python 2.7 runtime (see its own README) | tooling |
| `ticpp_2.5.3/` | TinyXML++ C++ wrapper, prebuilt with `TIXML_USE_TICPP`. See the XML routing section above | LlamaXML shim only |
| `vld/` | Visual Leak Detector. Build wiring is commented out — kept as dormant dev tooling per the codebase convention | n/a (dormant) |

---

## When to add something to `lib/` vs `ThirdPartyLibs/`

Add to **`ThirdPartyLibs/`** if:

- The lib has a recognizable upstream that still ships versioned releases
- The standard layout fits (`Include/`, `lib/<arch>/<config>/`)
- Multiple projects across the solution will consume it
- The `$(XxxLib)` name-property pattern works for arch/config selection

Add to **`lib/`** if:

- The upstream is dormant, gone, or a one-off extract (frozen SDK
  headers, dead C++ wrapper, etc.)
- One or two specific projects consume it; the rest of the solution
  doesn't care
- The build wiring is unusual enough that the standard global props
  would do the wrong thing
- "Best we can find" applies — there isn't a newer version to track

If you're unsure, lean toward `ThirdPartyLibs/`. Things that genuinely
belong in `lib/` tend to announce themselves.
