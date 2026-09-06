# 构建脚本：MSVC + core-ui 静态库，产出零依赖单 exe
# 用法: .\build.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

$coreuiSrc = Join-Path $root 'vendor\core-ui'
$buildDir  = Join-Path $root 'build-coreui'
$coreuiLib = Join-Path $buildDir 'core-ui.lib'

$VsPath  = "G:\Program Files\Microsoft Visual Studio\18\Community"
$vcvars  = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeExe= Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninjaExe= Join-Path $VsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

if (-not (Test-Path $vcvars))  { throw "vcvars64.bat missing: $vcvars" }

# 导入 vcvars64 环境（INCLUDE / LIB / PATH），使 cl / link / rc 可用
$installerDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vcvarsPath = "$installerDir;$env:PATH"
Write-Host "Loading vcvars64 environment..."
$envBlock = & cmd.exe /c "set `"PATH=$vcvarsPath`" && `"$vcvars`" >nul && set" 2>$null
foreach ($line in $envBlock) {
    if ($line -match '^([^=]+)=(.*)$') {
        $n = $Matches[1]; $v = $Matches[2]
        if ($n -in @('PROMPT','CD','_','COMSPEC')) { continue }
        Set-Item -Path "env:$n" -Value $v -ErrorAction SilentlyContinue
    }
}
if (-not $env:INCLUDE) { throw "vcvars import failed: INCLUDE empty" }

# 1) 构建 core-ui 静态库（首次配置 + 增量重建：库内源码有改动时也会重编）
if (-not (Test-Path (Join-Path $buildDir 'build.ninja'))) {
    Write-Host "Configuring core-ui..."
    & $cmakeExe -S $coreuiSrc -B $buildDir -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_MAKE_PROGRAM="$ninjaExe" `
      -DCMAKE_C_COMPILER=cl `
      -DCMAKE_CXX_COMPILER=cl `
      -DCMAKE_SYSTEM_VERSION=10.0.22621.0 `
      -DUI_CORE_STATIC=ON `
      -DUI_CORE_MSVC_STATIC_CRT=ON
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
}
Write-Host "Building core-ui static library (incremental)..."
& $cmakeExe --build $buildDir --target core-ui
if ($LASTEXITCODE -ne 0) { throw "core-ui build failed ($LASTEXITCODE)" }

# 2) 生成 app_uix.embed.h（把 src/app.uix 烤成 C++ 内联 char 数组）
$embed = Join-Path $coreuiSrc 'cmake\embed_text.cmake'
$embedOut = Join-Path $root 'src\app_uix.embed.h'
& $cmakeExe -DSRC="$root\src\app.uix" -DDST="$embedOut" -DVAR=k_app_uix -P $embed
if ($LASTEXITCODE -ne 0) { throw "embed header gen failed ($LASTEXITCODE)" }

# 3) 编译资源（清单：Common Controls v6 + PerMonitorV2 DPI）
& rc.exe /nologo /fo app.res app.rc
if ($LASTEXITCODE -ne 0) { throw "rc failed ($LASTEXITCODE)" }

# 4) 编译源码（/MT 与 core-ui 静态 CRT 匹配；/Os 优化体积）
& cl.exe /nologo /c /std:c++17 /O2 /Os /MT /EHsc /utf-8 `
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUI_CORE_STATIC `
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DNTDDI_VERSION=0x0A000004 `
    /D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS `
    /I "$coreuiSrc\include" /I "$root\src" `
    src\main.cpp src\data.cpp src\hooks.cpp src\autostart.cpp src\export.cpp
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }

# 5) 链接
$staticLibs = @(
    $coreuiLib,
    (Join-Path $buildDir '_deps\quickjs-build\qjs.lib'),
    (Join-Path $buildDir 'third_party\lunasvg\lunasvg.lib'),
    (Join-Path $buildDir 'third_party\lunasvg\plutovg\plutovg.lib')
)
$sysLibs = @(
    'd3d11.lib','dxguid.lib','gdiplus.lib','imm32.lib','ole32.lib'
)
& link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:KeyMouseTracker.exe /OPT:REF /OPT:ICF `
    main.obj data.obj hooks.obj autostart.obj export.obj app.res `
    $staticLibs $sysLibs
if ($LASTEXITCODE -ne 0) { throw "link failed ($LASTEXITCODE)" }

$exe = Get-Item "KeyMouseTracker.exe"
Write-Host ""
Write-Host ("Built: {0}  ({1:N0} bytes, {2:N2} KB)" -f $exe.Name, $exe.Length, ($exe.Length/1KB))