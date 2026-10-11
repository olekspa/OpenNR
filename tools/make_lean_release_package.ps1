<#
.SYNOPSIS
  Builds the redistributable "plugin-only" (lean) OpenNR release archive from an
  already-built, verified package folder.

.DESCRIPTION
  Only OpenNR-owned or GPL-licensed content is packaged: the plugin DLL, shaders,
  feature INIs, translations/themes/icons, fonts that ship with a licence text,
  the changelog/branding/eye-tracking notes and the licence/notice files.

  Never packaged (see docs/THIRD_PARTY_BINARIES.md for where users get them):
  every other .dll (NVIDIA nvngx_*/sl.*, AMD FidelityFX, RenderDoc,
  imgui-vr-helper), nvngx_dlssnr.dll, .pdb, .exe, TerrainHelper.esp/.ini,
  the Renderdoc, Streamline, StreamlineDX12 and FidelityFX folders, fonts without licence text.

  A deny-list self-test runs first (synthetic bad trees must be rejected), the
  staged tree is checked before compression, the archive listing is checked after
  compression, then `7z t` is run. A MANIFEST.txt with SHA-256 per file is
  written into the archive. Existing outputs are never overwritten.

.PARAMETER PackageDir  Verified package folder (default E:\OpenNR_Builds\2.22.2\verify).
.PARAMETER OutputDir   New/empty-of-collisions output folder.
.PARAMETER Version     Version string for the archive name (default: taken from VERSION at the repo root).
.PARAMETER NoticeDir   Folder holding LICENSE, EXCEPTIONS.md, NOTICE.md, THIRD_PARTY_NOTICES.md
                       (default: repo root, then the PUBLISH-DOCS worktree). Missing files become
                       clearly marked placeholders unless -RequireNotices is given.
.PARAMETER RequireNotices  Fail instead of writing placeholders.
.PARAMETER KeepStage   Keep the staging folder after archiving.
.PARAMETER SelfTestOnly  Run only the deny-list self-test.
#>
[CmdletBinding()]
param(
    [string]$PackageDir = 'E:\OpenNR_Builds\2.22.2\verify',
    [string]$OutputDir = 'E:\OpenNR_Builds\2.22.2-public',
    [string]$Version,
    [string]$NoticeDir,
    [string]$SevenZip,
    [switch]$RequireNotices,
    [switch]$KeepStage,
    [switch]$SelfTestOnly
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$maxBytes = 30MB
$pluginRel = 'SKSE/Plugins/CommunityShaders.dll'

function Get-Rel([string]$root, [string]$full) {
    $r = $full.Substring($root.TrimEnd('\').Length).TrimStart('\')
    return ($r -replace '\\', '/')
}

# ---------------------------------------------------------------------------
# Deny-list check. Returns a list of violation strings (empty = clean).
# $Items: objects with Rel (forward-slash relative path) and Length (bytes).
# ---------------------------------------------------------------------------
function Test-LeanDenyList($Items) {
    $v = New-Object System.Collections.Generic.List[string]
    foreach ($i in $Items) {
        $rel = $i.Rel
        $name = ($rel -split '/')[-1]
        $lower = $rel.ToLowerInvariant()
        $nl = $name.ToLowerInvariant()
        $ext = [System.IO.Path]::GetExtension($nl)
        if ($ext -eq '.dll' -and $lower -ne $pluginRel.ToLowerInvariant()) { $v.Add("DLL other than the plugin: $rel") }
        if ($nl -like 'nvngx*') { $v.Add("nvngx* file: $rel") }
        if ($nl -like 'sl.*') { $v.Add("sl.* file: $rel") }
        if ($nl -like 'amd_fidelityfx*' -or $nl -like 'renderdoc*.dll' -or $nl -like 'imgui-vr-helper*') { $v.Add("third-party runtime: $rel") }
        if ($ext -in '.pdb', '.exe', '.esp', '.esm', '.esl', '.lib', '.exp', '.obj', '.onnx', '.engine', '.pt', '.pth', '.safetensors') { $v.Add("denied extension ($ext): $rel") }
        if ($lower -match '(^|/)(renderdoc|streamline|streamlinedx12|fidelityfx)(/|$)' -and $lower -notmatch '^shaders/(features|upscaling/[^/]+\.hlsl)') { $v.Add("denied third-party folder: $rel") }
        if ($nl -eq 'terrainhelper.ini') { $v.Add("TerrainHelper (redistribution unverified): $rel") }
        if ($i.Length -gt $maxBytes) { $v.Add("file larger than 30 MB ($($i.Length) bytes): $rel") }
    }
    return $v
}

function Invoke-DenySelfTest {
    $bad = @(
        'SKSE/Plugins/imgui-vr-helper.dll', 'Shaders/Upscaling/Streamline/nvngx_dlss.dll', 'Shaders/Upscaling/Streamline/nvngx_dlssnr.dll',
        'Shaders/Upscaling/Streamline/sl.interposer.dll', 'Shaders/Upscaling/FidelityFX/amd_fidelityfx_loader_dx12.dll',
        'Renderdoc/renderdoc.dll', 'SKSE/Plugins/CommunityShaders.pdb', 'tool.exe', 'TerrainHelper.esp', 'Shaders/Features/TerrainHelper.ini',
        'SKSE/Plugins/Other.dll', 'Shaders/Upscaling/StreamlineDX12/license.txt', 'model.onnx'
    )
    foreach ($b in $bad) {
        $r = @(Test-LeanDenyList @([pscustomobject]@{ Rel = $b; Length = 10 }))
        if ($r.Count -eq 0) { throw "Deny-list self-test FAILED: '$b' was not rejected" }
    }
    $r = @(Test-LeanDenyList @([pscustomobject]@{ Rel = 'big/file.dds'; Length = ($maxBytes + 1) }))
    if ($r.Count -eq 0) { throw 'Deny-list self-test FAILED: oversize file not rejected' }
    $good = @($pluginRel, 'Shaders/Common/Color.hlsli', 'Shaders/Features/Upscaling.ini', 'Shaders/Upscaling/RCAS/RCAS.hlsl', 'OPENNR-2.22.2-CHANGELOG.md', 'Interface/CommunityShaders/Fonts/Inter/OFL.txt')
    foreach ($g in $good) {
        $r = @(Test-LeanDenyList @([pscustomobject]@{ Rel = $g; Length = 10 }))
        if ($r.Count -ne 0) { throw "Deny-list self-test FAILED: '$g' wrongly rejected: $($r -join '; ')" }
    }
    Write-Host ("Deny-list self-test passed ({0} bad paths rejected, {1} good paths accepted)." -f ($bad.Count + 1), $good.Count)
}

Invoke-DenySelfTest
if ($SelfTestOnly) { return }

# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $PackageDir -PathType Container)) { throw "Package folder not found: $PackageDir" }
if (-not (Test-Path -LiteralPath (Join-Path $PackageDir 'SKSE\Plugins\CommunityShaders.dll'))) { throw "CommunityShaders.dll missing from $PackageDir" }
if (-not $Version) {
    $vf = Join-Path $repoRoot 'VERSION'
    if (Test-Path $vf) { $Version = (Get-Content -LiteralPath $vf -TotalCount 1).Trim() }
    if (-not $Version) { throw 'Pass -Version' }
}
if (-not $SevenZip) {
    $cmd = Get-Command 7z -ErrorAction SilentlyContinue
    if ($cmd) { $SevenZip = $cmd.Source }
    elseif (Test-Path 'C:\Program Files\7-Zip\7z.exe') { $SevenZip = 'C:\Program Files\7-Zip\7z.exe' }
    else { throw '7z not found; pass -SevenZip' }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$archive = Join-Path $OutputDir ("OpenNR-{0}-plugin-only.7z" -f $Version)
$stage = Join-Path $OutputDir ("_stage-OpenNR-{0}-plugin-only" -f $Version)
if (Test-Path -LiteralPath $archive) { throw "Refusing to overwrite existing archive: $archive" }
if (Test-Path -LiteralPath $stage) { throw "Refusing to reuse existing staging folder: $stage" }

# --- allow-list selection ---------------------------------------------------
$allowedExt = '.hlsl', '.hlsli', '.ini', '.json', '.png', '.dds', '.ttf', '.md', '.txt', '.toml', '.wpc', '.nif'
$fontLicenseDirs = @{}
Get-ChildItem -LiteralPath (Join-Path $PackageDir 'Interface\CommunityShaders\Fonts') -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    $has = Get-ChildItem -LiteralPath $_.FullName -File | Where-Object { $_.Name -match '(?i)(OFL|licen[cs]e|apache|copying)' -and $_.Extension -in '.txt', '.md', '' }
    if ($has) { $fontLicenseDirs[$_.Name.ToLowerInvariant()] = $true }
}

$selected = New-Object System.Collections.Generic.List[object]
$skipped = New-Object System.Collections.Generic.List[string]
Get-ChildItem -LiteralPath $PackageDir -Recurse -File -Force | ForEach-Object {
    $rel = Get-Rel $PackageDir $_.FullName
    $lower = $rel.ToLowerInvariant()
    $ext = $_.Extension.ToLowerInvariant()
    $keep = $false
    if ($lower -eq $pluginRel.ToLowerInvariant()) { $keep = $true }
    elseif ($lower -match '^(renderdoc|openxr)/') { }
    elseif ($lower -match '^shaders/upscaling/(streamline|streamlinedx12|fidelityfx)/') { }
    elseif ($lower -eq 'shaders/features/terrainhelper.ini' -or $lower -eq 'shaders/features/renderdoc.ini') { }
    elseif ($lower -match '^interface/communityshaders/opennr logo/') { $keep = ($ext -eq '.png') }  # owner-created logo; owner confirmed ownership
    elseif ($lower -match '^interface/communityshaders/fonts/') {
        $parts = $lower -split '/'
        # fonts only when their own folder ships a licence text; loose fonts (fa-solid-900.ttf) are excluded
        if ($parts.Count -ge 5 -and $fontLicenseDirs.ContainsKey($parts[3])) { $keep = ($ext -in '.ttf', '.txt') }
    }
    elseif ($lower -match '^[^/]+$') {
        $keep = ($lower -like 'opennr-*-changelog.md' -or $lower -eq 'opennr-branding.txt' -or $lower -eq 'opennr-eyetracking.md')
    }
    elseif ($_.Name -eq 'LICENSE' -and $lower -like 'shaders/*') { $keep = $true }
    elseif ($ext -in $allowedExt -and $ext -ne '.dll') { $keep = $true }
    if ($keep) { $selected.Add([pscustomobject]@{ Rel = $rel; Full = $_.FullName; Length = $_.Length }) }
    else { $skipped.Add($rel) }
}

$pre = @(Test-LeanDenyList $selected)
if ($pre.Count -gt 0) { throw ("Deny-list violations in selection:`n  " + ($pre -join "`n  ")) }

# --- stage ------------------------------------------------------------------
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($s in $selected) {
    $dst = Join-Path $stage ($s.Rel -replace '/', '\')
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
    Copy-Item -LiteralPath $s.Full -Destination $dst
}

# --- licence / notice files ---------------------------------------------------
$noticeNames = 'LICENSE', 'EXCEPTIONS.md', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md'
$noticeRoots = @()
if ($NoticeDir) { $noticeRoots += $NoticeDir }
$noticeRoots += $repoRoot
$noticeRoots += 'C:\Users\oleks\.codex\worktrees\opennr-public-docs\OpenNR-VR'
$placeholders = @()
foreach ($n in $noticeNames) {
    $found = $null
    foreach ($r in $noticeRoots) {
        $p = Join-Path $r $n
        if (Test-Path -LiteralPath $p -PathType Leaf) { $found = $p; break }
    }
    if ($found) { Copy-Item -LiteralPath $found -Destination (Join-Path $stage $n); Write-Host "notice: $n <- $found" }
    else {
        if ($RequireNotices) { throw "Notice file missing: $n" }
        $placeholders += $n
        Set-Content -LiteralPath (Join-Path $stage $n) -Encoding UTF8 -Value "PLACEHOLDER: $n was not available when this archive was built. Do not publish this archive until the real file is included."
        Write-Warning "PLACEHOLDER written for $n (real file not found)"
    }
}

$readme = @"
OpenNR $Version - plugin-only package
=====================================
This archive contains only OpenNR-owned or GPL-licensed content: the plugin
(SKSE/Plugins/CommunityShaders.dll), shaders, feature INIs, translations,
themes, icons and notes. It deliberately contains NO NVIDIA, AMD, RenderDoc or
imgui-vr-helper binaries and no nvngx_dlssnr.dll.

To run it you must add the third-party components yourself from their official
vendor sources (nvngx_dlssnr.dll is supplied by the user); see
docs/THIRD_PARTY_BINARIES.md in the source repository for the list, download
links and destination folders. Licence terms: LICENSE, EXCEPTIONS.md,
NOTICE.md, THIRD_PARTY_NOTICES.md. File hashes: MANIFEST.txt.
"@
Set-Content -LiteralPath (Join-Path $stage 'README-PLUGIN-ONLY.txt') -Encoding UTF8 -Value $readme

# --- manifest ----------------------------------------------------------------
$files = Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("OpenNR $Version plugin-only package - SHA-256 manifest")
$lines.Add("Generated: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss'))Z")
if ($placeholders.Count) { $lines.Add("PLACEHOLDER notice files: $($placeholders -join ', ')") }
$lines.Add('Owner-created asset (OpenNR logo, ownership confirmed by the project owner; see its own LICENSE terms): Interface/CommunityShaders/OpenNR Logo/*.png')
$lines.Add('')
foreach ($f in $files) {
    $h = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $lines.Add(("{0}  {1}" -f $h, (Get-Rel $stage $f.FullName)))
}
Set-Content -LiteralPath (Join-Path $stage 'MANIFEST.txt') -Encoding UTF8 -Value $lines

# --- staged-tree deny check ---------------------------------------------------
$staged = Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object { [pscustomobject]@{ Rel = (Get-Rel $stage $_.FullName); Length = $_.Length } }
$viol = @(Test-LeanDenyList $staged)
if ($viol.Count -gt 0) { throw ("Deny-list violations in staged tree:`n  " + ($viol -join "`n  ")) }

# --- archive -----------------------------------------------------------------
Push-Location $stage
try { & $SevenZip a -t7z -mx=9 -bso0 -bsp0 $archive '*' | Out-Null; if ($LASTEXITCODE -ne 0) { throw "7z a failed ($LASTEXITCODE)" } }
finally { Pop-Location }

# listing check (independent of the staging folder) + integrity test
$listing = & $SevenZip l -slt $archive
$entries = @()
$cur = $null
foreach ($l in $listing) {
    if ($l -like 'Path = *') { $cur = $l.Substring(7) }
    elseif ($l -like 'Size = *' -and $cur) { $entries += [pscustomobject]@{ Rel = ($cur -replace '\\', '/'); Length = [int64]$l.Substring(7) }; $cur = $null }
}
$entries = @($entries | Where-Object { $_.Rel -ne $archive.Replace('\', '/') -and $_.Rel -notlike '*.7z' })
$aviol = @(Test-LeanDenyList $entries)
if ($aviol.Count -gt 0) { throw ("Deny-list violations in archive listing:`n  " + ($aviol -join "`n  ")) }
& $SevenZip t $archive
if ($LASTEXITCODE -ne 0) { throw '7z t failed' }

$sha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath ($archive + '.sha256') -Encoding ASCII -Value ("{0}  {1}" -f $sha, (Split-Path -Leaf $archive))
Set-Content -LiteralPath (Join-Path $OutputDir ("OpenNR-{0}-plugin-only.excluded.txt" -f $Version)) -Encoding UTF8 -Value $skipped

# --- informational: build paths embedded in the plugin ------------------------
$bytes = [System.IO.File]::ReadAllBytes((Join-Path $stage 'SKSE\Plugins\CommunityShaders.dll'))
$text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
$mAscii = [regex]::Matches($text, 'C:\\Users\\[\x20-\x7e]{1,80}')
$mUtf16 = [regex]::Matches($text, 'C\x00:\x00\\\x00U\x00s\x00e\x00r\x00s\x00')
Write-Host ("INFO: 'C:\Users\' occurrences in CommunityShaders.dll: ASCII={0}, UTF-16={1} (informational; cannot be fixed after the build)" -f $mAscii.Count, $mUtf16.Count)
$mAscii | Select-Object -First 5 -ExpandProperty Value -Unique | ForEach-Object { Write-Host "  e.g. $_" }

if (-not $KeepStage) { Remove-Item -LiteralPath $stage -Recurse -Force }
Write-Host ("Archive: {0}`nSize: {1} bytes`nSHA-256: {2}`nFiles: {3} (+MANIFEST)" -f $archive, (Get-Item $archive).Length, $sha, $entries.Count)
if ($placeholders.Count) { Write-Warning "Archive contains PLACEHOLDER notice files: $($placeholders -join ', ')" }
