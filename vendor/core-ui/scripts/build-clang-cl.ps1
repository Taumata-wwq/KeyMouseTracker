#requires -Version 5.1
<#
.SYNOPSIS
    Configure and build core-ui with clang-cl + Ninja against the MSVC toolchain.

.DESCRIPTION
    1. Imports vcvars64.bat env vars (INCLUDE / LIB / PATH) into this PowerShell
       session — clang-cl needs the MSVC headers and libs.
    2. Runs `cmake -G Ninja` with clang-cl as the C/C++ compiler, into
       <repo>/clang-cl-build/.
    3. Builds.

.PARAMETER Target
    Single CMake target (e.g. core-ui, ui-demo, release-package). Default = all.

.PARAMETER Clean
    Remove the clang-cl-build/ dir before configuring.

.PARAMETER StaticCrt
    Static-link the MSVC C/C++ runtime (/MT). Output DLL has zero runtime deps.

.PARAMETER VsPath
    Override VS BuildTools path. Default: probe known locations.

.PARAMETER ClangClPath
    Override clang-cl.exe path. Default: probe PATH.
#>
[CmdletBinding()]
param(
    [string] $Target,
    [switch] $Clean,
    [switch] $StaticCrt,
    [string] $VsPath,
    [string] $ClangClPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repo 'clang-cl-build'

# ---------- locate vcvars64.bat ----------
if (-not $VsPath) {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community"
    )
    $VsPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $VsPath) { throw "Visual Studio not found. Pass -VsPath." }
}
$vcvars = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat missing at $vcvars" }

# ---------- locate clang-cl ----------
if (-not $ClangClPath) {
    $cl = Get-Command clang-cl -ErrorAction SilentlyContinue
    if ($cl) { $ClangClPath = $cl.Source }
    else { throw "clang-cl not in PATH. Pass -ClangClPath." }
}
if (-not (Test-Path $ClangClPath)) { throw "clang-cl not found at $ClangClPath" }

Write-Host "VS BuildTools : $VsPath"
Write-Host "vcvars64.bat  : $vcvars"
Write-Host "clang-cl      : $ClangClPath"
Write-Host "build dir     : $buildDir"
Write-Host ""

# ---------- import vcvars64 env ----------
# Run vcvars64.bat in cmd.exe and capture the resulting environment block,
# then apply the diff (INCLUDE / LIB / LIBPATH / PATH) into our session.
# vcvars64.bat internally calls vswhere.exe which lives in the VS Installer
# dir — make sure that's on PATH for the cmd.exe sub-shell.
$installerDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vcvarsPath = "$installerDir;$env:PATH"
Write-Host "Loading vcvars64 environment..."
$envBlock = & cmd.exe /c "set `"PATH=$vcvarsPath`" && `"$vcvars`" >nul && set" 2>$null
foreach ($line in $envBlock) {
    if ($line -match '^([^=]+)=(.*)$') {
        $name = $Matches[1]
        $value = $Matches[2]
        # Skip volatile/process-state vars
        if ($name -in @('PROMPT', 'CD', '_', 'COMSPEC', 'PROMPT')) { continue }
        Set-Item -Path "env:$name" -Value $value -ErrorAction SilentlyContinue
    }
}
if (-not $env:INCLUDE) { throw "vcvars import failed: \$env:INCLUDE empty" }

# Forward-slash for CMake friendliness
$ClangClFwd = $ClangClPath -replace '\\','/'

# ---------- clean ----------
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning $buildDir..."
    Remove-Item -Recurse -Force $buildDir
}

# ---------- configure ----------
$needsConfigure = -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))
if ($needsConfigure) {
    Write-Host ""
    Write-Host "Configuring..."
    $args = @(
        '-S', $repo,
        '-B', $buildDir,
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=$ClangClFwd",
        "-DCMAKE_CXX_COMPILER=$ClangClFwd"
    )
    if ($StaticCrt) { $args += '-DUI_CORE_MSVC_STATIC_CRT=ON' }
    # Force llvm-rc instead of Windows SDK rc.exe (10.0.26100 hangs in some
    # subprocess contexts — preprocesses the .rc but then never writes the .res).
    $llvmRc = Join-Path (Split-Path -Parent $ClangClPath) 'llvm-rc.exe'
    if (Test-Path $llvmRc) {
        $llvmRcFwd = $llvmRc -replace '\\','/'
        $args += "-DCMAKE_RC_COMPILER=$llvmRcFwd"
    }
    & cmake @args
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}

# ---------- build ----------
Write-Host ""
Write-Host "Building..."
$buildArgs = @('--build', $buildDir)
if ($Target) { $buildArgs += '--target', $Target }
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

Write-Host ""
Write-Host "Done. Output in $buildDir"
