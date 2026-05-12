# wiz3D - deploy_to_releases.ps1
#
# Copies freshly-built DLLs from:
#   - bin/Release/<arch>/                 (S3DDriver.sln)
#   - wiz3D-proxy/bin/Release/<arch>/     (wiz3D-proxy.sln)
#   - NvDirectMode/bin/Release/<arch>/    (NvDirectMode.sln)
# into the appropriate releases/wiz3D/<api>/<arch>/ subfolders so a release is
# ready to install into a game directory. Run AFTER building all three slns.
#
# This script does NOT handle:
# - Vendor-path proxy DLLs that auto-deploy via their own vcxproj OutDir:
#   atidxx32/64.dll (AmdQbProxy), atiadlxy.dll (AmdAdlProxy),
#   dxgi.dll (DxgiVendorProxy), d3d12.dll (D3d12VendorProxy x64 only),
#   nvapi/nvapi64.dll (NvApiProxy) -- these go into releases/wiz3D/hd3d/.
#
# Usage:
#   .\bin\deploy_to_releases.ps1                # both Win32 + x64
#   .\bin\deploy_to_releases.ps1 -Arch Win32    # x86 only
#   .\bin\deploy_to_releases.ps1 -Arch x64      # x64 only

[CmdletBinding()]
param(
    [ValidateSet('Win32', 'x64', 'both')]
    [string]$Arch = 'both'
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$archs = if ($Arch -eq 'both') { @('Win32', 'x64') } else { @($Arch) }

# Common dependency DLLs shared across most release subfolders
$commonDeps    = @('S3DAPI.dll', 'S3DDevIL.dll', 'S3DUtils.dll', 'ZLOg.dll')
$dx10ExtraDeps = @('S3Dilu.dll')           # dx10-11 only
$openglDeps    = @('S3DAPI.dll', 'S3DUtils.dll', 'ZLOg.dll')   # OpenGL skips DevIL/ilu

# Output methods supported by each render API. Most APIs ship the same set;
# OpenGL only ships the SR weave method (limited stereo support upstream).
$standardOMs = @(
    'AnaglyphOutput.dll',
    'InterlacedOutput.dll',
    'S3DMarkedOutput.dll',
    'S3DOutput.dll',
    'SideBySideOutput.dll',
    'SimulatedRealityWeaveOutput.dll'
)
$openglOMs = @('SimulatedRealityWeaveOutput.dll')

function Copy-Files {
    param(
        [Parameter(Mandatory)] [string] $SrcDir,
        [Parameter(Mandatory)] [string] $DstDir,
        [Parameter(Mandatory)] [string[]] $Files,
        [Parameter(Mandatory)] [string] $Tag,
        [string[]] $FallbackDirs = @()
    )
    if (-not (Test-Path $DstDir)) {
        New-Item -ItemType Directory -Path $DstDir -Force | Out-Null
    }
    $copied  = 0
    $missing = @()
    foreach ($file in $Files) {
        $src = Join-Path $SrcDir $file
        if (-not (Test-Path $src)) {
            foreach ($fb in $FallbackDirs) {
                $cand = Join-Path $fb $file
                if (Test-Path $cand) { $src = $cand; break }
            }
        }
        $dst = Join-Path $DstDir $file
        if (Test-Path $src) {
            Copy-Item -Path $src -Destination $dst -Force
            $copied++
        } else {
            $missing += $file
        }
    }
    Write-Host ("  {0,-22}  copied {1,2}/{2,-2}  -> {3}" -f $Tag, $copied, $Files.Count, $DstDir)
    if ($missing.Count -gt 0) {
        foreach ($m in $missing) { Write-Host "      MISSING: $m" -ForegroundColor Yellow }
    }
}

foreach ($archName in $archs) {
    $archAlias = if ($archName -eq 'Win32') { 'x86' } else { 'x64' }
    $outDirName = if ($archName -eq 'Win32') { 'Win32' } else { 'Win64' }
    $binDir    = Join-Path $repoRoot "bin\Release\$outDirName"
    $omSrcDir  = Join-Path $binDir   'OutputMethods'
    # Vendored upstream DevIL artifacts (S3DDevIL.dll, S3Dilu.dll) — not built by sln
    $devilFallback = Join-Path $repoRoot "lib\DevIL\lib\$archAlias"
    # wiz3D-proxy.sln output (entry-point DLLs games actually load: d3d9.dll, etc)
    $proxyBinDir       = Join-Path $repoRoot "wiz3D-proxy\bin\Release\$archName"
    $nvDirectModeBin   = Join-Path $repoRoot "NvDirectMode\bin\Release\$archName"

    if (-not (Test-Path $binDir)) {
        Write-Warning "Skipping ${archName}: $binDir not found (build first?)"
        continue
    }

    Write-Host ""
    Write-Host "=== $archName -> $archAlias ===" -ForegroundColor Cyan

    # --- dx7 (32-bit only — no x64 release) ---
    if ($archAlias -eq 'x86') {
        $dst = Join-Path $repoRoot 'releases\wiz3D\dx7'
        Copy-Files -SrcDir $binDir   -DstDir $dst                          `
                   -Files (@('S3DWrapperD3D7.dll', 'S3DWrapperD3D9.dll') + $commonDeps)  `
                   -Tag   'dx7 wrappers+deps'
        Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
                   -Files $standardOMs -Tag 'dx7 OutputMethods'
        Copy-Files -SrcDir $proxyBinDir -DstDir $dst                          `
                   -Files @('d3d9.dll', 'ddraw.dll')                         `
                   -Tag   'dx7 proxies'
    }

    # --- dx8 (32-bit only) ---
    if ($archAlias -eq 'x86') {
        $dst = Join-Path $repoRoot 'releases\wiz3D\dx8'
        Copy-Files -SrcDir $binDir   -DstDir $dst                          `
                   -Files (@('S3DWrapperD3D8.dll', 'S3DWrapperD3D9.dll') + $commonDeps)  `
                   -Tag   'dx8 wrappers+deps'
        Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
                   -Files $standardOMs -Tag 'dx8 OutputMethods'
        Copy-Files -SrcDir $proxyBinDir -DstDir $dst                          `
                   -Files @('d3d8.dll', 'd3d9.dll')                          `
                   -Tag   'dx8 proxies'
    }

    # --- dx9 (both archs) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\dx9\$archAlias"
    Copy-Files -SrcDir $binDir   -DstDir $dst                          `
               -Files (@('S3DWrapperD3D9.dll') + $commonDeps)          `
               -Tag   "dx9/$archAlias wrappers+deps"                   `
               -FallbackDirs @($devilFallback)
    Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
               -Files $standardOMs -Tag "dx9/$archAlias OutputMethods"
    Copy-Files -SrcDir $proxyBinDir -DstDir $dst                          `
               -Files @('d3d9.dll')                                       `
               -Tag   "dx9/$archAlias proxies"

    # --- dx10-11 (both archs) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\dx10-11\$archAlias"
    Copy-Files -SrcDir $binDir   -DstDir $dst                                          `
               -Files (@('S3DWrapperD3D10.dll') + $commonDeps + $dx10ExtraDeps)        `
               -Tag   "dx10-11/$archAlias wrappers+deps"                               `
               -FallbackDirs @($devilFallback)
    Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods')              `
               -Files $standardOMs -Tag "dx10-11/$archAlias OutputMethods"
    Copy-Files -SrcDir $proxyBinDir -DstDir $dst                                       `
               -Files @('d3d10.dll', 'd3d11.dll', 'dxgi.dll')                          `
               -Tag   "dx10-11/$archAlias proxies"

    # --- opengl quad-buffer stereo (both archs) ---
    # Folder name spells out the capability: this wrapper captures native
    # OpenGL quad-buffer stereo (PFD_STEREO / GL_BACK_LEFT/RIGHT) from games
    # that produce it (id Tech 3/4, CAD apps, sims). It does NOT generate
    # stereo from mono OpenGL games.
    $dst = Join-Path $repoRoot "releases\wiz3D\opengl-quad-buffer-stereo\$archAlias"
    Copy-Files -SrcDir $binDir   -DstDir $dst                          `
               -Files (@('S3DWrapperOGL.dll') + $openglDeps)           `
               -Tag   "opengl-qbs/$archAlias wrappers+deps"
    Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
               -Files $openglOMs -Tag "opengl-qbs/$archAlias OutputMethods"
    Copy-Files -SrcDir $proxyBinDir -DstDir $dst                          `
               -Files @('opengl32.dll')                                   `
               -Tag   "opengl-qbs/$archAlias proxies"

    # --- dx12 stereo proxy (both archs; no wrapper sln output yet) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\dx12\$archAlias"
    Copy-Files -SrcDir $proxyBinDir -DstDir $dst                          `
               -Files @('d3d12.dll')                                      `
               -Tag   "dx12/$archAlias proxy"

    # --- vulkan stereo proxy (both archs; no wrapper sln output yet) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\vulkan\$archAlias"
    Copy-Files -SrcDir $proxyBinDir -DstDir $dst                          `
               -Files @('vulkan-1.dll')                                   `
               -Tag   "vulkan/$archAlias proxy"

    # --- NvDirectMode (3D Vision Direct Mode proxies) ---
    # Layout: releases/wiz3D/3d-vision-direct/<api>/<archAlias>/<dll>
    # Each leaf gets its API-specific proxy DLL; nvapi[64].dll gets spread
    # in by the Spread-File loop after the per-arch loops finish.
    if (Test-Path $nvDirectModeBin) {
        $ndmBase = Join-Path $repoRoot "releases\wiz3D\3d-vision-direct"
        if (-not (Test-Path $ndmBase)) { New-Item -ItemType Directory -Path $ndmBase -Force | Out-Null }
        # Top-level ReadMe — install instructions, test targets, known limits.
        # Source is checked-in at NvDirectMode\ReadMe.txt; release tree is
        # gitignored, so it gets re-laid each deploy.
        $ndmReadmeSrc = Join-Path $repoRoot 'NvDirectMode\ReadMe.txt'
        if (Test-Path $ndmReadmeSrc) {
            Copy-Item -Path $ndmReadmeSrc -Destination (Join-Path $ndmBase 'ReadMe.txt') -Force
        }

        # Per-leaf payload: proxy DLL + 3DVision_Config.xml + Uninstall_3DVisionDirect.bat.
        # nvapi[64].dll is laid down separately by the Spread-File pass below.
        $cfgSrc       = Join-Path $repoRoot 'NvDirectMode\3DVision_Config.xml'
        $uninstallSrc = Join-Path $repoRoot 'NvDirectMode\Uninstall_3DVisionDirect.bat'
        function Copy-NdmExtras {
            param([string]$LeafDir)
            if (Test-Path $cfgSrc)       { Copy-Item -Path $cfgSrc       -Destination $LeafDir -Force }
            if (Test-Path $uninstallSrc) { Copy-Item -Path $uninstallSrc -Destination $LeafDir -Force }
        }

        $dx9Dst    = Join-Path $ndmBase "dx9\$archAlias"
        $dx10Dst   = Join-Path $ndmBase "dx10\$archAlias"
        $dx11Dst   = Join-Path $ndmBase "dx11\$archAlias"
        # NOTE: no NvDirectMode/opengl release leaf. No shipped OpenGL game uses
        # NvAPI_Stereo_SetActiveEye (Direct Mode is DX-only in practice). OpenGL
        # games that want 3D Vision stereo go through a DX-bridge wrapper like
        # Helifax's OGL-3DVision-Wrapper, and our DX9/10/11 magic-header capture
        # picks that traffic up automatically. The opengl32 proxy stays in source
        # (NvDirectMode/opengl32/) but isn't deployed.

        Copy-Files -SrcDir $nvDirectModeBin -DstDir $dx9Dst   -Files @('d3d9.dll')    -Tag "ndm/dx9/$archAlias"
        Copy-NdmExtras -LeafDir $dx9Dst
        Copy-Files -SrcDir $nvDirectModeBin -DstDir $dx10Dst  -Files @('d3d10.dll')   -Tag "ndm/dx10/$archAlias"
        Copy-NdmExtras -LeafDir $dx10Dst
        # dx11 leaf gets BOTH d3d11.dll AND dxgi.dll — game can use either
        # D3D11CreateDeviceAndSwapChain (caught by d3d11.dll) or
        # CreateDXGIFactory -> CreateSwapChain (caught by dxgi.dll).
        Copy-Files -SrcDir $nvDirectModeBin -DstDir $dx11Dst  -Files @('d3d11.dll','dxgi.dll') -Tag "ndm/dx11/$archAlias"
        Copy-NdmExtras -LeafDir $dx11Dst
    } else {
        Write-Host ("  ndm/$archAlias            SKIP (NvDirectMode bin not built: $nvDirectModeBin)") -ForegroundColor Yellow
    }

    # hd3d/* not handled here — those vendor proxy DLLs auto-deploy via vcxproj OutDir.
}

# --- Spread auto-deployed shared DLLs across all api subfolders ---
# NvApiProxy auto-deploys to releases/wiz3D/dx9/<arch>/ via its vcxproj OutDir,
# but games using other render APIs need their own copy. Mirror them here.
# (Other OutputMethods incl. SimulatedRealityWeaveOutput build to the standard
#  bin/Release/<arch>/OutputMethods/ and are distributed by the loops above.)
function Spread-File {
    param(
        [Parameter(Mandatory)] [string] $SrcPath,
        [Parameter(Mandatory)] [string[]] $DstDirs,
        [Parameter(Mandatory)] [string] $Tag
    )
    if (-not (Test-Path $SrcPath)) {
        Write-Host ("  {0,-30}  SOURCE MISSING: {1}" -f $Tag, $SrcPath) -ForegroundColor Yellow
        return
    }
    foreach ($d in $DstDirs) {
        if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Copy-Item -Path $SrcPath -Destination $d -Force
    }
    Write-Host ("  {0,-30}  spread to {1} dirs" -f $Tag, $DstDirs.Count)
}

Write-Host ""
Write-Host "=== Spreading shared DLLs ===" -ForegroundColor Cyan
$relRoot = Join-Path $repoRoot 'releases\wiz3D'
foreach ($archAlias in $archs | ForEach-Object { if ($_ -eq 'Win32') { 'x86' } else { 'x64' } }) {
    $nvapiName = if ($archAlias -eq 'x86') { 'nvapi.dll' } else { 'nvapi64.dll' }
    $srcNvapi  = Join-Path $relRoot "dx9\$archAlias\$nvapiName"
    # nvapi spreads into:
    #   - regular wiz3D dx10-11 / opengl-quad-buffer-stereo folders (3D Vision-aware
    #     games using passive)
    #   - all four 3d-vision-direct/<api>/<arch>/ leaves (Direct Mode games need NvApiProxy
    #     beside the NvDirectMode proxy DLL because they call NvAPI_Stereo_SetActiveEye etc.)
    $nvapiTargets = @(
        "$relRoot\dx10-11\$archAlias",
        "$relRoot\opengl-quad-buffer-stereo\$archAlias",
        "$relRoot\3d-vision-direct\dx9\$archAlias",
        "$relRoot\3d-vision-direct\dx10\$archAlias",
        "$relRoot\3d-vision-direct\dx11\$archAlias"
    )
    Spread-File -SrcPath $srcNvapi -DstDirs $nvapiTargets -Tag "$nvapiName ($archAlias)"
}

Write-Host ""
Write-Host "Deploy complete." -ForegroundColor Green
