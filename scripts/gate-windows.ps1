# Gate G0 on Windows x64: configure and build Debug and Release, run CTest,
# check the committed headless hashes, and prove the executable does not depend
# on SDL2.dll. The only entry point a human or CI needs.
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Initialize-Msvc {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere; the Visual Studio installer index is missing"
    }
    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $installation) {
        throw "No Visual Studio installation with the C++ workload was found"
    }
    $vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path $vcvars)) {
        throw "vcvarsall.bat not found at $vcvars"
    }
    $environment = & cmd.exe /d /s /c "`"$vcvars`" x64 >nul 2>&1 && set"
    foreach ($line in $environment) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
        }
    }
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "cl.exe is not on PATH after vcvarsall x64"
    }
    Write-Host "toolchain: $(& cl.exe 2>&1 | Select-Object -First 1)"
}

function Invoke-Checked {
    param([string]$Label, [scriptblock]$Action)
    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed (exit code $LASTEXITCODE)"
    }
}

function Get-FrameHash {
    param([string]$Exe, [string]$State)
    $output = (& $Exe --headless --state $State | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe --headless --state $State failed (exit code $LASTEXITCODE)"
    }
    if ($output -notmatch 'frame hash ([0-9A-F]{16}) 1440x900') {
        throw "Unexpected headless output: $output"
    }
    return $Matches[1]
}

function Assert-Hash {
    param([string]$Exe, [string]$State, [string]$Golden)
    $actual = Get-FrameHash -Exe $Exe -State $State
    $expected = (Get-Content (Join-Path $repo "goldens\$Golden") -Raw).Trim()
    if ($actual -ne $expected) {
        throw "$State hash $actual does not match goldens\$Golden ($expected)"
    }
    Write-Host "hash $State $actual (matches goldens/$Golden)"
}

Initialize-Msvc

foreach ($preset in @('windows-msvc', 'windows-msvc-release')) {
    Write-Host "== configure $preset"
    Invoke-Checked "configure $preset" { cmake --preset $preset }
    Write-Host "== build $preset"
    Invoke-Checked "build $preset" { cmake --build --preset $preset }
    Write-Host "== test $preset"
    Invoke-Checked "test $preset" { ctest --preset $preset }
}

foreach ($preset in @('windows-msvc', 'windows-msvc-release')) {
    $exe = Join-Path $repo "build\$preset\krdbg_demo.exe"
    Assert-Hash -Exe $exe -State 'empty' -Golden 'empty.hash'
    Assert-Hash -Exe $exe -State 'loaded' -Golden 'loaded.hash'
}

$release = Join-Path $repo 'build\windows-msvc-release\krdbg_demo.exe'
New-Item -ItemType Directory -Force (Join-Path $repo 'build\gate') | Out-Null
$screenshot = Join-Path $repo 'build\gate\windows-loaded.ppm'
& $release --headless --state loaded --screenshot $screenshot | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $screenshot)) {
    throw "Could not write the headless screenshot"
}
Write-Host "screenshot $screenshot"

Write-Host "== dependency check"
$dependencies = (& dumpbin /dependents $release | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin /dependents failed (exit code $LASTEXITCODE)"
}
if ($dependencies -match 'SDL2\.dll') {
    throw "krdbg_demo.exe depends on SDL2.dll"
}
$sdl = Get-ChildItem -Path $repo -Recurse -Filter 'SDL2.dll' -ErrorAction SilentlyContinue
if ($sdl) {
    throw "SDL2.dll found in the tree: $($sdl[0].FullName)"
}
Write-Host "no SDL2.dll dependency and no SDL2.dll in the tree"

Write-Host 'GATE WINDOWS: PASS'
