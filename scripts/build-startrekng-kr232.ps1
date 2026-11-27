# Builds KRCompiler's startrekng multi-file C++ example for kr232.
#
# The sources stay in the KRCompiler tree. Objects and images are written to a
# transient directory under this repository's build/ so a clean build recreates
# them without touching the sources:
#
#   build/startrekng-kr232/
#     startrekng_script.kr232   (main_script.cpp, deterministic session)
#     startrekng.kr232          (main.cpp, interactive)
#     *.o, build-info.txt
#
# The recipe is the one in krcompiler/examples/startrekng/README.md: every unit
# compiled with `--target=kr232 --far-calls`, the target's crt0, libc_full and
# fileio_full compiled from the same source tree, then linked with --entry=_start.
# Compiling from the KRCompiler root keeps the recorded source paths relative
# (`examples/startrekng/...`), so the debugger resolves them with
# `--source-root ../krcompiler`.
#
# Usage, from this repository:
#   powershell -File scripts/build-startrekng-kr232.ps1
#   powershell -File scripts/build-startrekng-kr232.ps1 -Clean
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix beside this repository. KRCOMPILER_ROOT overrides the
# sibling KRCompiler tree (default: ../krcompiler).
[CmdletBinding()]
param(
    [string]$KrCompilerRoot = $env:KRCOMPILER_ROOT,
    [string]$Prefix = $env:KRG_PREFIX,
    [string]$BuildDirectory = 'build/startrekng-kr232',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $KrCompilerRoot) {
    $KrCompilerRoot = Join-Path (Split-Path -Parent $repo) 'krcompiler'
}
if (-not (Test-Path $KrCompilerRoot)) {
    throw "KRCompiler tree not found at '$KrCompilerRoot'; set KRCOMPILER_ROOT"
}
$KrCompilerRoot = (Resolve-Path $KrCompilerRoot).Path

if (-not $Prefix) {
    $Prefix = $env:KRG_PREFIX
}
if (-not $Prefix -and $env:KRG_KRC) {
    $Prefix = Split-Path -Parent (Split-Path -Parent $env:KRG_KRC)
}
if (-not $Prefix) {
    $candidate = Join-Path (Split-Path -Parent $repo) 'build\family-prefix'
    if (Test-Path $candidate) {
        $Prefix = $candidate
    }
}
if (-not $Prefix -or -not (Test-Path $Prefix)) {
    throw 'compiler package prefix not found: set KRG_PREFIX or KRG_KRC'
}
$Prefix = (Resolve-Path $Prefix).Path

$krc = Join-Path $Prefix 'bin\krc.exe'
$krld = Join-Path $Prefix 'bin\krld.exe'
$sysroot = Join-Path $Prefix 'share\krc\sysroot'
$runtime = Join-Path $KrCompilerRoot 'targets\kr232\runtime'
foreach ($required in @($krc, $krld, $sysroot, $runtime)) {
    if (-not (Test-Path $required)) {
        throw "missing '$required'; the prefix or the KRCompiler tree is incomplete"
    }
}

$output = Join-Path $repo $BuildDirectory
if ($Clean -and (Test-Path $output)) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

$units = @('console', 'geometry', 'galaxy', 'ship', 'scanner', 'weapons',
           'computer', 'game')
$common = @('-c', '--target=kr232', '--far-calls', "--sysroot=$sysroot")

Push-Location $KrCompilerRoot
try {
    $objects = @()
    foreach ($unit in $units) {
        $object = Join-Path $output "$unit.o"
        & $krc @common "examples/startrekng/$unit.cpp" -o $object
        if ($LASTEXITCODE -ne 0) {
            throw "krc failed for examples/startrekng/$unit.cpp"
        }
        $objects += $object
    }

    $crt0 = Join-Path $output 'crt0.o'
    & $krc @common 'targets/kr232/runtime/crt0.s' -o $crt0
    if ($LASTEXITCODE -ne 0) { throw 'krc failed for targets/kr232/runtime/crt0.s' }

    $libc = Join-Path $output 'libc.o'
    & $krc @common 'targets/kr232/runtime/libc_full.c' -o $libc
    if ($LASTEXITCODE -ne 0) { throw 'krc failed for targets/kr232/runtime/libc_full.c' }

    $fileio = Join-Path $output 'fileio.o'
    & $krc @common 'targets/kr232/runtime/fileio_full.s' -o $fileio
    if ($LASTEXITCODE -ne 0) { throw 'krc failed for targets/kr232/runtime/fileio_full.s' }

    $images = @(
        @{ Entry = 'main';        Image = 'startrekng.kr232' },
        @{ Entry = 'main_script'; Image = 'startrekng_script.kr232' }
    )
    foreach ($candidate in $images) {
        $entryObject = Join-Path $output "$($candidate.Entry).o"
        & $krc @common "examples/startrekng/$($candidate.Entry).cpp" -o $entryObject
        if ($LASTEXITCODE -ne 0) {
            throw "krc failed for examples/startrekng/$($candidate.Entry).cpp"
        }
        $image = Join-Path $output $candidate.Image
        & $krld '--entry=_start' $crt0 @objects $entryObject $libc $fileio -o $image
        if ($LASTEXITCODE -ne 0) {
            throw "krld failed for $image"
        }
        Write-Host "wrote $image"
    }
} finally {
    Pop-Location
}

$krcVersion = (& $krc --version 2>&1 | Out-String).Trim()
$krldVersion = (& $krld --version 2>&1 | Out-String).Trim()
$info = @(
    "source: $KrCompilerRoot/examples/startrekng",
    "prefix: $Prefix",
    "krc: $krcVersion",
    "krld: $krldVersion",
    "built: $(Get-Date -Format o)",
    'images: startrekng.kr232 (main.cpp), startrekng_script.kr232 (main_script.cpp)'
)
Set-Content -LiteralPath (Join-Path $output 'build-info.txt') -Value $info
Write-Host "startrekng kr232 build in $output"
