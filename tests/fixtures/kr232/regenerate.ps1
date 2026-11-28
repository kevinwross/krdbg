# Rebuilds the kr232 fixtures from source with M2's toolchain.
#
# The containers are checked in because the adapter tests must run without a
# compiler, but they are not hand-made: this script is how they are produced.
# Set KR232_SOURCE to the kr232 checkout and KRC/KRLD to the compiler's tools.
#
# Every compile runs from the root that owns its source and names the source
# relative to it, because the source path is embedded in the image's debug data:
# an absolute path made the fixture reproducible only on the machine that
# recorded it, and `CheckFixtureDrift.cmake` could not rebuild it elsewhere.
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$krc = if ($env:KRC) { $env:KRC } else { Join-Path $repo '..\krcompiler\build\windows-msvc-debug\host\bin\Debug\krc.exe' }
$krld = if ($env:KRLD) { $env:KRLD } else { Join-Path $repo '..\krcompiler\build\windows-msvc-debug\host\bin\Debug\krld.exe' }
$kr232 = if ($env:KR232_SOURCE) { $env:KR232_SOURCE } else { Join-Path $repo '..\kr232' }
$sources = Join-Path $repo '..\krcompiler'
$work = Join-Path $repo 'build\kr232-fixtures'

if (-not (Test-Path $krc)) { throw "krc not found at $krc; set KRC" }
if (-not (Test-Path $krld)) { throw "krld not found at $krld; set KRLD" }
New-Item -ItemType Directory -Path $work -Force | Out-Null
New-Item -ItemType Directory -Path $PSScriptRoot -Force | Out-Null

$sourcesRoot = (Resolve-Path $sources).Path
$kr232Root = (Resolve-Path $kr232).Path
$fixtureRoot = (Resolve-Path $PSScriptRoot).Path
$workText = (Resolve-Path $work).Path -replace '\\', '/'

function Compile-From([string] $root, [string] $source, [string] $output, [string[]] $extra = @()) {
    Push-Location $root
    try {
        & $krc -c --target kr232 @extra $source -o $output
        if ($LASTEXITCODE -ne 0) { throw "compiling $source failed" }
    } finally {
        Pop-Location
    }
}

function Link-Fixture([string] $root, [string] $image, [string[]] $objects, [string[]] $extra = @()) {
    Push-Location $root
    try {
        & $krld @extra --entry=_start @objects -o (Join-Path $fixtureRoot $image)
        if ($LASTEXITCODE -ne 0) { throw "$image failed" }
    } finally {
        Pop-Location
    }
    Write-Host "wrote $image"
}

# crt0 is the entry every fixture links with.
Compile-From $sourcesRoot 'targets/kr232/runtime/crt0.s' "$workText/crt0.o"

# loaded.kr232: the two-instruction program the layout and memory tests use.
Compile-From $sourcesRoot 'tests/fixtures/c/kr232_min.c' "$workText/min.o"
Link-Fixture $sourcesRoot 'loaded.kr232' @("$workText/crt0.o", "$workText/min.o")

# nested_frames.kr232: two frames on the stack for the unwind test.
Compile-From $sourcesRoot 'tests/fixtures/c/kr232_nested_frames.c' "$workText/nested.o"
Link-Fixture $sourcesRoot 'nested_frames.kr232' @("$workText/crt0.o", "$workText/nested.o")

# session.kr232: a console byte and a committed frame for the device tests.
Compile-From $sourcesRoot 'tests/fixtures/c/kr232_session.c' "$workText/session.o" @('-I', 'sysroot/kr232/include')
Compile-From $sourcesRoot 'targets/kr232/runtime/cons.c' "$workText/cons.o" @('-I', 'sysroot/kr232/include')
Compile-From $sourcesRoot 'targets/kr232/runtime/gfx.c' "$workText/gfx.o" @('-I', 'sysroot/kr232/include')
Link-Fixture $sourcesRoot 'session.kr232' @("$workText/crt0.o", "$workText/session.o", "$workText/cons.o", "$workText/gfx.o")

# kernel.kr232 is the kernel the adapter runs user programs under, and
# userprint.kr232 is a program that prints through it and exits 3.
Compile-From $kr232Root 'targets/kr232/kernel/kernel.s' "$workText/kernel.o"
Link-Fixture $kr232Root 'kernel.kr232' @("$workText/kernel.o")

Compile-From $fixtureRoot 'userprint.s' "$workText/userprint.o"
Link-Fixture $fixtureRoot 'userprint.kr232' @("$workText/userprint.o") @('--base=0x08000000')

# aggregate.kr232: an array of structs and a scalar global. It links against
# crt0 only; the fixture has no libc calls.
Compile-From $repo 'tests/fixtures/kr232/aggregate.c' "$workText/aggregate.o"
Link-Fixture $repo 'aggregate.kr232' @("$workText/crt0.o", "$workText/aggregate.o")

# Symbol fixtures for tests/test_kr232_symbols.cpp. They live in this tree and
# are compiled from the repository root so the recorded source path is
# `tests/fixtures/kr232/src/...`, which the tests resolve under
# KRDBG_SOURCE_ROOT. No libc: the fixtures use int data only, so each image is
# a few kilobytes and links crt0 alone. -O0 keeps line stepping independent of
# optimiser decisions; promoted_o1.kr232 is the deliberate -O1 case.
$symbols = 'tests/fixtures/kr232/src'
Compile-From $repo "$symbols/symbols_cpp.cpp" "$workText/symbols_cpp.o" @('-O0')
Compile-From $repo "$symbols/symbols_pair_math.cpp" "$workText/symbols_pair_math.o" @('-O0', '-I', $symbols)
Compile-From $repo "$symbols/symbols_pair_main.cpp" "$workText/symbols_pair_main.o" @('-O0', '-I', $symbols)
Compile-From $repo "$symbols/include_heavy.cpp" "$workText/include_heavy.o" @('-O0', '-I', "$symbols/include")
Compile-From $repo "$symbols/promoted.cpp" "$workText/promoted.o" @('-O1')
Compile-From $repo "$symbols/symbols_asm.s" "$workText/symbols_asm.o"
Link-Fixture $repo 'symbols_cpp.kr232' @("$workText/crt0.o", "$workText/symbols_cpp.o")
Link-Fixture $repo 'symbols_pair.kr232' @("$workText/crt0.o", "$workText/symbols_pair_math.o", "$workText/symbols_pair_main.o")
Link-Fixture $repo 'symbols_include.kr232' @("$workText/crt0.o", "$workText/include_heavy.o")
Link-Fixture $repo 'promoted_o1.kr232' @("$workText/crt0.o", "$workText/promoted.o")
# The assembly fixture defines its own _start, so it links without crt0.
Link-Fixture $repo 'symbols_asm.kr232' @("$workText/symbols_asm.o")

Write-Host 'fixtures rebuilt; the adapter tests read them by name'
