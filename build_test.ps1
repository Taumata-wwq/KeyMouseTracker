# 单元测试构建 + 运行
# 用法: .\build_test.ps1
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

# 定位 Visual Studio：优先本机硬编码路径，缺失时回退 vswhere（兼容 CI）
function Resolve-VsPath {
    try {
        $hardcoded = "G:\Program Files\Microsoft Visual Studio\18\Community"
        $hcVcvars = Join-Path $hardcoded 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $hcVcvars -ErrorAction SilentlyContinue) { return $hardcoded }
    } catch { }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere -ErrorAction SilentlyContinue) {
        $p = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($p) { return $p }
    }
    return $null
}
# 导入 vcvars64 环境（INCLUDE / LIB / PATH），使 cl / link 可用
$VsPath  = Resolve-VsPath
if (-not $VsPath) { throw "Visual Studio (with C++ tools) not found. Install VS or edit the path in build_test.ps1." }
$vcvars  = Join-Path $VsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat missing: $vcvars" }
$installerDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$envBlock = & cmd.exe /c "set `"PATH=$installerDir;$env:PATH`" && `"$vcvars`" >nul && set" 2>$null
foreach ($line in $envBlock) {
    if ($line -match '^([^=]+)=(.*)$') {
        $n = $Matches[1]; $v = $Matches[2]
        if ($n -in @('PROMPT','CD','_','COMSPEC')) { continue }
        Set-Item -Path "env:$n" -Value $v -ErrorAction SilentlyContinue
    }
}
if (-not $env:INCLUDE) { throw "vcvars import failed: INCLUDE empty" }

New-Item -ItemType Directory -Force -Path (Join-Path $root 'tests') | Out-Null

# unity build：test_main.cpp 直接包含 data.cpp / export.cpp，以访问其 static 纯逻辑
& cl.exe /nologo /c /std:c++17 /O2 /MT /EHsc /utf-8 `
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX `
    /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /DNTDDI_VERSION=0x0A000004 `
    /D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS `
    /I "$root\src" `
    tests\test_main.cpp /Fo:tests\test_main.obj
if ($LASTEXITCODE -ne 0) { throw "test compile failed ($LASTEXITCODE)" }

& link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:tests\test_main.exe `
    tests\test_main.obj kernel32.lib user32.lib gdi32.lib shell32.lib
if ($LASTEXITCODE -ne 0) { throw "test link failed ($LASTEXITCODE)" }

& tests\test_main.exe
if ($LASTEXITCODE -ne 0) { throw "tests FAILED ($LASTEXITCODE)" }
Write-Host ""
Write-Host "All tests passed."
