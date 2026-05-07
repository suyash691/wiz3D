# wiz3D - deploy_to_releases.ps1
#
# Copies freshly-built DLLs from bin/Release/<arch>/ into the appropriate
# releases/wiz3D/<api>/<arch>/ subfolders so a release is ready to install
# into a game directory. Run AFTER building S3DDriver.sln.
#
# This script does NOT handle:
# - Proxy DLLs that already auto-deploy via their own vcxproj OutDir:
#   atidxx32/64.dll (AmdQbProxy), atiadlxy.dll (AmdAdlProxy),
#   dxgi.dll (DxgiVendorProxy), d3d12.dll (D3d12VendorProxy x64 only),
#   nvapi/nvapi64.dll (NvApiProxy)
# - Wrapper-style proxies that come from the separate wiz3D-proxy.sln:
#   d3d9.dll, ddraw.dll, d3d8.dll, d3d10.dll, d3d11.dll, opengl32.dll
#   Build that solution separately and copy its outputs by hand for now.
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
        [Parameter(Mandatory)] [string] $Tag
    )
    if (-not (Test-Path $DstDir)) {
        New-Item -ItemType Directory -Path $DstDir -Force | Out-Null
    }
    $copied  = 0
    $missing = @()
    foreach ($file in $Files) {
        $src = Join-Path $SrcDir $file
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
    $binDir    = Join-Path $repoRoot "bin\Release\$archName"
    $omSrcDir  = Join-Path $binDir   'OutputMethods'

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
    }

    # --- dx8 (32-bit only) ---
    if ($archAlias -eq 'x86') {
        $dst = Join-Path $repoRoot 'releases\wiz3D\dx8'
        Copy-Files -SrcDir $binDir   -DstDir $dst                          `
                   -Files (@('S3DWrapperD3D8.dll', 'S3DWrapperD3D9.dll') + $commonDeps)  `
                   -Tag   'dx8 wrappers+deps'
        Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
                   -Files $standardOMs -Tag 'dx8 OutputMethods'
    }

    # --- dx9 (both archs) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\dx9\$archAlias"
    Copy-Files -SrcDir $binDir   -DstDir $dst                          `
               -Files (@('S3DWrapperD3D9.dll') + $commonDeps)          `
               -Tag   "dx9/$archAlias wrappers+deps"
    Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
               -Files $standardOMs -Tag "dx9/$archAlias OutputMethods"

    # --- dx10-11 (both archs) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\dx10-11\$archAlias"
    Copy-Files -SrcDir $binDir   -DstDir $dst                                          `
               -Files (@('S3DWrapperD3D10.dll') + $commonDeps + $dx10ExtraDeps)        `
               -Tag   "dx10-11/$archAlias wrappers+deps"
    Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods')              `
               -Files $standardOMs -Tag "dx10-11/$archAlias OutputMethods"

    # --- opengl (both archs) ---
    $dst = Join-Path $repoRoot "releases\wiz3D\opengl\$archAlias"
    Copy-Files -SrcDir $binDir   -DstDir $dst                          `
               -Files (@('S3DWrapperOGL.dll') + $openglDeps)           `
               -Tag   "opengl/$archAlias wrappers+deps"
    Copy-Files -SrcDir $omSrcDir -DstDir (Join-Path $dst 'OutputMethods') `
               -Files $openglOMs -Tag "opengl/$archAlias OutputMethods"

    # hd3d/* not handled here — those proxy DLLs auto-deploy via vcxproj OutDir.
}

Write-Host ""
Write-Host "Deploy complete." -ForegroundColor Green
Write-Host "Reminder: d3d9.dll, ddraw.dll, d3d8.dll, d3d10.dll, d3d11.dll, and"
Write-Host "opengl32.dll come from the separate wiz3D-proxy/wiz3D-proxy.sln."
Write-Host "Build that solution and copy those outputs into the matching release"
Write-Host "subfolder by hand."
