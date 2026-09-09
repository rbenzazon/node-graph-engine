# Configure + build + test with MSVC (VsDevCmd) + Ninja.
# Usage:
#   .\scripts\build.ps1
#   .\scripts\build.ps1 -Clean
#   .\scripts\build.ps1 -Config Release

param(
  [ValidateSet('Debug', 'Release')]
  [string]$Config = 'Debug',
  [switch]$Clean,
  [switch]$NoTest
)

$ErrorActionPreference = 'Stop'
$Root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $Root

. (Join-Path $PSScriptRoot 'dev_env.ps1')

$BuildDir = if ($Config -eq 'Release') { 'build-release' } else { 'build' }

if ($Clean -and (Test-Path $BuildDir)) {
  Remove-Item -Recurse -Force $BuildDir
}

$preset = if ($Config -eq 'Release') { 'release' } else { 'default' }

if (-not (Test-Path $BuildDir)) {
  cmake --preset $preset
} else {
  # Re-configure if needed; cheap if unchanged.
  cmake --preset $preset
}

cmake --build --preset $preset

if (-not $NoTest) {
  ctest --preset $preset
}

$demo = Join-Path $BuildDir 'demos\minimal_pipeline.exe'
if (Test-Path $demo) {
  & $demo
}
