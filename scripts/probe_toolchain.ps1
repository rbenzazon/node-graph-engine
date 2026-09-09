$ErrorActionPreference = 'Continue'
Write-Output '=== vswhere ==='
$vw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
Write-Output "vswhere_exists=$([bool](Test-Path $vw))"
if (Test-Path $vw) {
  Write-Output 'displayName:'
  & $vw -all -products * -property displayName
  Write-Output 'installationPath:'
  & $vw -all -products * -property installationPath
  Write-Output 'VC tools path:'
  & $vw -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}

Write-Output '=== roots ==='
$roots = @(
  'C:\Program Files\Microsoft Visual Studio',
  'C:\Program Files (x86)\Microsoft Visual Studio',
  'C:\Program Files (x86)\Windows Kits',
  'C:\Program Files\Windows Kits'
)
foreach ($r in $roots) {
  Write-Output "root=$r exists=$([bool](Test-Path $r))"
  if (Test-Path $r) {
    Get-ChildItem $r -ErrorAction SilentlyContinue | ForEach-Object { "  $($_.Name)" }
  }
}

Write-Output '=== cl.exe ==='
Get-ChildItem 'C:\Program Files\Microsoft Visual Studio', 'C:\Program Files (x86)\Microsoft Visual Studio' -Recurse -Filter 'cl.exe' -ErrorAction SilentlyContinue |
  Select-Object -First 8 -ExpandProperty FullName

Write-Output '=== kernel32.lib ==='
Get-ChildItem 'C:\Program Files (x86)\Windows Kits', 'C:\Program Files\Windows Kits' -Recurse -Filter 'kernel32.lib' -ErrorAction SilentlyContinue |
  Select-Object -First 8 -ExpandProperty FullName

Write-Output '=== g++.exe ==='
$gppHits = @()
foreach ($p in @('C:\mingw64', 'C:\WinLibs', 'C:\Tools', 'C:\dev', 'C:\msys64', "$env:LOCALAPPDATA\Microsoft\WinGet\Packages", 'C:\Program Files', 'C:\Program Data')) {
  if (Test-Path $p) {
    $gppHits += Get-ChildItem $p -Recurse -Filter 'g++.exe' -ErrorAction SilentlyContinue | Select-Object -First 3 -ExpandProperty FullName
  }
}
$gppHits | Select-Object -Unique | Select-Object -First 10

Write-Output '=== PATH matches ==='
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
($env:Path -split ';') | Where-Object { $_ -match 'mingw|winlib|MSVC|Windows Kits|LLVM|cmake|ninja|BuildTools' }

Write-Output '=== Get-Command ==='
foreach ($t in 'cmake', 'ninja', 'cl', 'g++', 'gcc', 'clang++') {
  $c = Get-Command $t -ErrorAction SilentlyContinue
  if ($c) { Write-Output "$t=$($c.Source)" } else { Write-Output "$t=MISSING" }
}

Write-Output '=== winget list (filtered) ==='
winget list 2>$null | Select-String -Pattern 'BuildTools|Visual Studio|WinLibs|MinGW|LLVM|CMake|Ninja'
