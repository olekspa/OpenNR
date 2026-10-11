<#
.SYNOPSIS
  Copies locally held third-party binaries into the (git-ignored) tracked-path
  locations of this checkout, so the private/local full ("AIO") package workflow
  works after a clean clone.

.DESCRIPTION
  OpenNR does not commit NVIDIA, AMD, RenderDoc or imgui-vr-helper binaries.
  Keep your own copies in one directory (see docs/THIRD_PARTY_BINARIES.md) that
  mirrors the paths below, relative to runtime/open-shaders:

    features/RenderDoc/Renderdoc/renderdoc.dll
    features/Terrain Helper/TerrainHelper.esp
    features/Upscaling/Shaders/Upscaling/StreamlineDX12/{nvngx_dlssg,sl.common,sl.dlss_g,sl.interposer,sl.pcl,sl.reflex}.dll
    package/SKSE/Plugins/imgui-vr-helper.dll
    package/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll   (supplied by you; never committed)

  Run this BEFORE configure/package. Existing identical files are left alone;
  differing files are only replaced with -Force. Nothing is ever git-added.

.PARAMETER ThirdPartyDir  Source directory. Default: $env:OPENNR_THIRDPARTY_DIR.
.PARAMETER RepoRoot       Checkout root. Default: parent of this script's directory.
.PARAMETER DryRun         Report what would be copied without copying.
.PARAMETER Force          Overwrite destination files whose content differs.
#>
[CmdletBinding()]
param(
    [string]$ThirdPartyDir = $env:OPENNR_THIRDPARTY_DIR,
    [string]$RepoRoot,
    [switch]$DryRun,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }

if ([string]::IsNullOrWhiteSpace($ThirdPartyDir)) {
    throw "No source directory. Pass -ThirdPartyDir or set the OPENNR_THIRDPARTY_DIR environment variable (see docs/THIRD_PARTY_BINARIES.md)."
}
if (-not (Test-Path -LiteralPath $ThirdPartyDir -PathType Container)) {
    throw "Third-party staging directory not found: $ThirdPartyDir"
}
$shadersRoot = Join-Path $RepoRoot 'runtime\open-shaders'
if (-not (Test-Path -LiteralPath (Join-Path $shadersRoot 'CMakeLists.txt'))) {
    throw "Not an OpenNR checkout (runtime\open-shaders\CMakeLists.txt missing under $RepoRoot)"
}

$sl = 'features/Upscaling/Shaders/Upscaling/StreamlineDX12'
$items = @(
    @{ Rel = 'package/SKSE/Plugins/imgui-vr-helper.dll'; Need = 'required for configure (source contract); upstream: https://github.com/alandtse/imgui-vr-helper/releases'; Sha256 = 'bf2941d4513a37d526d37fd8bf657823e3dbf2c31a085ed28c6ee224c828eba6' },
    @{ Rel = 'package/Shaders/Upscaling/Streamline/nvngx_dlssnr.dll'; Need = 'required for configure/AIO package; supplied by you, never committed' },
    @{ Rel = 'features/RenderDoc/Renderdoc/renderdoc.dll'; Need = 'optional (RenderDoc feature payload); https://renderdoc.org/' },
    @{ Rel = 'features/Terrain Helper/TerrainHelper.esp'; Need = 'optional (Terrain Helper feature payload; redistribution permission not verified, never committed); Nexus Mods mod 143149' },
    @{ Rel = "$sl/nvngx_dlssg.dll"; Need = 'optional (staged from the Streamline SDK by the build anyway; only needed for tools/dlssg-repro)' },
    @{ Rel = "$sl/sl.common.dll"; Need = 'optional (see above)' },
    @{ Rel = "$sl/sl.dlss_g.dll"; Need = 'optional (see above)' },
    @{ Rel = "$sl/sl.interposer.dll"; Need = 'optional (see above)' },
    @{ Rel = "$sl/sl.pcl.dll"; Need = 'optional (see above)' },
    @{ Rel = "$sl/sl.reflex.dll"; Need = 'optional (see above)' }
)

$copied = 0; $same = 0; $missing = 0; $mismatch = 0
foreach ($it in $items) {
    $rel = $it.Rel -replace '/', '\'
    $src = Join-Path $ThirdPartyDir $rel
    $dst = Join-Path $shadersRoot $rel
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
        Write-Warning "MISSING in staging dir: $rel  [$($it.Need)]"
        $missing++
        continue
    }
    $srcHash = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($it.Sha256 -and $srcHash -ne $it.Sha256) {
        Write-Warning "HASH MISMATCH (expected pinned build) for $rel : $srcHash"
        $mismatch++
        continue
    }
    if (Test-Path -LiteralPath $dst -PathType Leaf) {
        $dstHash = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($dstHash -eq $srcHash) { Write-Host "up to date : $rel"; $same++; continue }
        if (-not $Force) { Write-Warning "differs, kept (use -Force to overwrite): $rel"; $mismatch++; continue }
    }
    if ($DryRun) { Write-Host "[dry-run] would copy $rel" }
    else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst -Force
        Write-Host "copied     : $rel"
    }
    $copied++
}
Write-Host ("Done. copied={0} up-to-date={1} missing={2} skipped/mismatch={3}{4}" -f $copied, $same, $missing, $mismatch, $(if ($DryRun) { ' (dry run)' } else { '' }))
Write-Host "These destinations are git-ignored; do not commit them."
if ($missing -gt 0) { exit 2 }
