# Loads VS 2022 Build Tools x64 env into the current PowerShell session.
# Usage: . .\scripts\dev_env.ps1

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
  throw "vswhere not found at $vswhere"
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
  throw 'No VS installation with MSVC VC Tools found.'
}

$vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) {
  throw "VsDevCmd.bat not found at $vsDevCmd"
}

$tmp = [System.IO.Path]::GetTempFileName()
cmd /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 -no_logo && set" > $tmp

Get-Content $tmp | ForEach-Object {
  if ($_ -match '^(.*?)=(.*)$') {
    $name = $matches[1]
    $value = $matches[2]
    [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
  }
}
Remove-Item $tmp -Force

Write-Host "MSVC dev env loaded from: $vsPath"
Write-Host "cl: $(Get-Command cl -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)"
Write-Host "WindowsSdkDir: $env:WindowsSdkDir"
