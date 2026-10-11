[CmdletBinding()]
param(
    [string]$BuildRoot,
    [string]$Dependencies,
    [string]$SourceRoot,
    [string[]]$Targets = @('CommunityShaders', 'cpp_tests'),
    [bool]$CaptureEnabled = $true,
    [switch]$ConfigureOnly,
    [switch]$DisableShaderTests,
    [switch]$OpenNRPerformanceOverlay,
    [string]$GpuJobLabel = 'OPENNR-SHADERS',
    # Release packages link with LTCG (/GL + /LTCG), as upstream shipping builds do.
    [bool]$LinkTimeOptimization = $true
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $SourceRoot) { $SourceRoot = Join-Path $repo 'runtime\open-shaders' }
$toolchainRoot = $env:OPENNR_TOOLCHAIN_ROOT
if (-not $toolchainRoot) { $toolchainRoot = Join-Path (Split-Path -Parent $repo) 'OpenNR-Toolchain' }
if (-not $env:OPENNR_DEPENDENCIES_ROOT) { $env:OPENNR_DEPENDENCIES_ROOT = '<OPENNR_DATA>\Dependencies\runtime-2.16.0' }
$python = Get-Command python -ErrorAction Stop
$pathTool = Join-Path $PSScriptRoot 'opennr_paths.py'
$pathArguments = @($pathTool, 'build')
if ($BuildRoot) { $pathArguments += @('--path', $BuildRoot) }
$BuildRoot = & $python.Source @pathArguments
if ($LASTEXITCODE -ne 0) { throw 'Invalid external build destination' }
$pathArguments = @($pathTool, 'dependencies')
if ($Dependencies) { $pathArguments += @('--path', $Dependencies) }
$Dependencies = & $python.Source @pathArguments
if ($LASTEXITCODE -ne 0) { throw 'Invalid external dependency destination' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ tools are required' }
$cmake = $env:OPENNR_CMAKE_EXE
if (-not $cmake) {
    $workspaceCMake = Join-Path $toolchainRoot 'cmake\4.4.3\bin\cmake.exe'
    if (Test-Path -LiteralPath $workspaceCMake) {
        $cmake = $workspaceCMake
    } else {
        $cmakeCommand = Get-Command cmake.exe -ErrorAction Stop
        $cmake = $cmakeCommand.Source
    }
}
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) { throw "CMake executable is missing: $cmake" }
$cmakeVersionText = (& $cmake --version | Select-Object -First 1)
if ($cmakeVersionText -notmatch 'cmake version ([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "Could not read CMake version from $cmake"
}
$cmakeVersion = [version]$Matches[1]
if ($cmakeVersion -lt [version]'4.2.0') {
    throw "OpenNR shader tests need CMake 4.2 or later; found CMake $cmakeVersion at $cmake. Set OPENNR_CMAKE_EXE to the workspace toolchain copy."
}
Write-Host "Using CMake $cmakeVersion at $cmake"
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$vcpkg = Join-Path $vs 'VC\vcpkg\scripts\buildsystems\vcpkg.cmake'
$shaderTestFramework = Join-Path $toolchainRoot 'dependencies\ShaderTestFramework'
$shaderTestsValue = if ($DisableShaderTests) { 'OFF' } else { 'ON' }
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$captureValue = if ($CaptureEnabled) { 'ON' } else { 'OFF' }
$ipoValue = if ($LinkTimeOptimization) { 'ON' } else { 'OFF' }
$performanceOverlayValue = if ($OpenNRPerformanceOverlay) { 'ON' } else { 'OFF' }
$arguments = @(
    '-S', "`"$SourceRoot`"", '-B', "`"$BuildRoot`"", '-G', '"Visual Studio 17 2022"', '-A', 'x64',
    "`"-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$vcpkg`"", '-DVCPKG_MANIFEST_INSTALL=OFF',
    "`"-DVCPKG_INSTALLED_DIR=$Dependencies\vcpkg_installed`"", '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md-release',
    "`"-DOPENNR_COMMONLIB_SOURCE_DIR=$Dependencies\CommonLibSSE-NG`"",
    "`"-DCOMMONLIB_PREBUILT_DIR=$Dependencies\CommonLib-prebuilt`"",
    "`"-DOPENNR_STREAMLINE_SOURCE_DIR=$Dependencies\Streamline-DX12`"",
    "`"-DOPENNR_FFX_SOURCE_DIR=$Dependencies\FidelityFX-SDK`"",
    "`"-DOPENNR_NVAPI_SOURCE_DIR=$Dependencies\nvapi`"",
    "`"-DOPENNR_DLSSNR_RUNTIME_SOURCE=$Dependencies\private-runtime\nvngx_dlssnr.dll`"",
    "`"-DOPENNR_IMGUI_HELPER_SOURCE=$Dependencies\private-runtime\imgui-vr-helper.dll`"",
    "`"-DDIST_PATH=$BuildRoot\dist`"",
    "`"-DFETCHCONTENT_SOURCE_DIR_CATCH2=$Dependencies\catch2-src`"",
    "`"-DFETCHCONTENT_SOURCE_DIR_IMGUIVRHELPER=$Dependencies\imguivrhelper-src`"",
    "`"-DFETCHCONTENT_SOURCE_DIR_HDE64=$Dependencies\hde64-src`"",
    "`"-DFETCHCONTENT_SOURCE_DIR_SHADERTESTFRAMEWORK=$shaderTestFramework`"",
    '-DAUTO_PLUGIN_DEPLOYMENT=OFF', '-DBUILD_CPP_TESTS=ON', "-DBUILD_SHADER_TESTS=$shaderTestsValue",
    "-DOPENNR_PERFORMANCE_OVERLAY=$performanceOverlayValue",
    "-DBUILD_OPENNR_CAPTURE=$captureValue", "-DAIO_INCLUDE_OPENNR_CAPTURE=$captureValue", '-DOPENNR_LEAN_PACKAGE=ON',
    '-DAIO_INCLUDE_NON_AUTOUPLOAD=OFF', '-DAIO_ZIP_TO_DIST=OFF', '-DZIP_TO_DIST=OFF',
    '-DSKSE_SUPPORT_XBYAK=ON', '-DENABLE_SKYRIM_SE=ON', '-DENABLE_SKYRIM_AE=ON', '-DENABLE_SKYRIM_VR=ON',
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=$ipoValue", '-DSC_DEVFAST_OPTS=OFF', '-DSC_COMPILE_PDB=OFF',
    '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
)
$brokerPython = '<OPENNR_DATA>\Tools\OpenNRTrainVenv\Scripts\python.exe'
$brokerScript = '<OPENNR_DATA>\Tools\CodexBridge\codex_bridge.py'
if (-not $DisableShaderTests -and (Test-Path -LiteralPath $brokerScript)) {
    if (-not (Test-Path -LiteralPath $brokerPython)) { throw 'GPU broker Python is missing' }
    $arguments += @(
        "`"-DOPENNR_GPU_BROKER_PYTHON:FILEPATH=$brokerPython`"",
        "`"-DOPENNR_GPU_BROKER_SCRIPT:FILEPATH=$brokerScript`"",
        "`"-DOPENNR_GPU_JOB_LABEL:STRING=$GpuJobLabel`""
    )
}
$lines = @('@echo off', "call `"$vcvars`"", 'if errorlevel 1 exit /b 1', "`"$cmake`" $($arguments -join ' ')", 'if errorlevel 1 exit /b 1')
if (-not $ConfigureOnly) {
    $lines += "`"$cmake`" --build `"$BuildRoot`" --config Release --target $($Targets -join ' ') --parallel 4"
    $lines += 'if errorlevel 1 exit /b 1'
}
$script = Join-Path $BuildRoot 'build-opennr.cmd'
[IO.File]::WriteAllLines($script, $lines)
$stdoutLog = Join-Path $BuildRoot 'build-opennr.stdout.log'
$stderrLog = Join-Path $BuildRoot 'build-opennr.stderr.log'
$commandLine = "/d /c `"$script`""
$process = Start-Process -FilePath $env:ComSpec -ArgumentList $commandLine -Wait -PassThru -NoNewWindow `
    -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
if ($process.ExitCode -ne 0) {
    Write-Host "OpenNR build stdout: $stdoutLog"
    Write-Host "OpenNR build stderr: $stderrLog"
    if (Test-Path -LiteralPath $stdoutLog) { Get-Content -LiteralPath $stdoutLog -Tail 160 | ForEach-Object { Write-Host $_ } }
    if (Test-Path -LiteralPath $stderrLog) { Get-Content -LiteralPath $stderrLog -Tail 160 | ForEach-Object { Write-Host $_ } }
    throw "OpenNR build failed: $($process.ExitCode)"
}
Write-Host "OpenNR build succeeded. Stdout: $stdoutLog; stderr: $stderrLog"
if (Test-Path -LiteralPath $stdoutLog) { Get-Content -LiteralPath $stdoutLog -Tail 40 | ForEach-Object { Write-Host $_ } }
if ((Test-Path -LiteralPath $stderrLog) -and (Get-Item -LiteralPath $stderrLog).Length -gt 0) {
    Get-Content -LiteralPath $stderrLog -Tail 40 | ForEach-Object { Write-Host $_ }
}
