# Clean build script - removes old artifacts and rebuilds
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

Write-Host "=== Cleaning old artifacts ==="

# Remove embed header (will be regenerated)
$embed = "src\app_uix.embed.h"
if (Test-Path $embed) {
    Remove-Item $embed -Force
    Write-Host "Removed: $embed"
}

# Remove build directory (core-ui + deps)
$buildDir = "build-coreui"
if (Test-Path $buildDir) {
    Remove-Item $buildDir -Recurse -Force
    Write-Host "Removed: $buildDir/"
}

# Remove any obj files
$objFiles = Get-ChildItem *.obj -ErrorAction SilentlyContinue
if ($objFiles) {
    $objFiles | Remove-Item -Force
    Write-Host "Removed: *.obj files"
}

# Remove old exe
$exePath = "KeyMouseTracker.exe"
if (Test-Path $exePath) {
    Remove-Item $exePath -Force
    Write-Host "Removed: $exePath"
}

# Remove resource file
$resFile = "app.res"
if (Test-Path $resFile) {
    Remove-Item $resFile -Force
    Write-Host "Removed: $resFile"
}

Write-Host ""
Write-Host "=== Cleanup complete ==="
Write-Host ""
Write-Host "Ready to build. Run: .\build.ps1"