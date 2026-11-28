# Rebuilds the kr216 fixtures from source with the staged kr216 toolchain.
#
# The containers are checked in because the adapter tests must run without a
# compiler, but they are not hand-made: this script is how they are produced.
# Set KRCOMPILER_SOURCE to the KRCompiler checkout and KRC/KRLD to its tools;
# the defaults are the staged prefix this work used.
#
# Every compile runs from the root that owns its source and names the source
# relative to it, because the source path is embedded in the image's debug
# data: an absolute path made the fixture reproducible only on the machine that
# recorded it, and CheckFixtureDrift.cmake could not rebuild it elsewhere.
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$krc = if ($env:KRC) { $env:KRC } else { Join-Path $repo 'build\krc-new-prefix\bin\krc.exe' }
$krld = if ($env:KRLD) { $env:KRLD } else { Join-Path $repo 'build\krc-new-prefix\bin\krld.exe' }
$krcompiler = if ($env:KRCOMPILER_SOURCE) { $env:KRCOMPILER_SOURCE } else { Join-Path $repo '..\krcompiler' }
$work = Join-Path $repo 'build\kr216-fixtures'

if (-not (Test-Path $krc)) { throw "krc not found at $krc; set KRC" }
if (-not (Test-Path $krld)) { throw "krld not found at $krld; set KRLD" }
New-Item -ItemType Directory -Path $work -Force | Out-Null
New-Item -ItemType Directory -Path $PSScriptRoot -Force | Out-Null

$repoRoot = (Resolve-Path $repo).Path
$compilerRoot = (Resolve-Path $krcompiler).Path
$fixtureRoot = (Resolve-Path $PSScriptRoot).Path
$workText = (Resolve-Path $work).Path -replace '\\', '/'

function Compile-From([string] $root, [string] $source, [string] $output, [string[]] $extra = @()) {
    Push-Location $root
    try {
        & $krc -c --target kr216 @extra $source -o $output
        if ($LASTEXITCODE -ne 0) { throw "compiling $source failed" }
    } finally {
        Pop-Location
    }
}

function Link-Fixture([string] $image, [string[]] $objects) {
    Push-Location $repoRoot
    try {
        & $krld --entry=_start @objects -o (Join-Path $fixtureRoot $image)
        if ($LASTEXITCODE -ne 0) { throw "$image failed" }
    } finally {
        Pop-Location
    }
    Write-Host "wrote $image"
}

# crt0 is the entry every fixture links with; the console fixture also reads
# through the runtime's write trampoline and the fixture-only read trampoline.
Compile-From $compilerRoot 'targets/kr216/runtime/crt0.s' "$workText/crt0.o"
Compile-From $compilerRoot 'targets/kr216/runtime/syscalls.s' "$workText/syscalls.o"
Compile-From $repoRoot 'tests/fixtures/kr216/src/console_read.s' "$workText/console_read.o"

# loaded.kr216: the two-instruction program the load, layout, reset and
# disassembly tests use.
Compile-From $repoRoot 'tests/fixtures/kr216/src/loaded.c' "$workText/loaded.o" @('-O0')
Link-Fixture 'loaded.kr216' @("$workText/crt0.o", "$workText/loaded.o")

# session.kr216: the C fixture. It has parameters, locals, globals, an
# aggregate, a three-frame call chain and the console.
Compile-From $repoRoot 'tests/fixtures/kr216/src/symbols_c.c' "$workText/symbols_c.o" @('-O0')
Link-Fixture 'session.kr216' @(
    "$workText/crt0.o", "$workText/symbols_c.o",
    "$workText/syscalls.o", "$workText/console_read.o")

# symbols_cpp.kr216: a class, a member function's `this` and a member global.
Compile-From $repoRoot 'tests/fixtures/kr216/src/symbols_cpp.cpp' "$workText/symbols_cpp.o" @('-O0')
Link-Fixture 'symbols_cpp.kr216' @("$workText/crt0.o", "$workText/symbols_cpp.o")

# symbols_asm.kr216: a hand-written `_start` that calls the C helper. The kr216
# assembler emits no KDBG records for .s text, so the image's source lines are
# the helper's.
Compile-From $repoRoot 'tests/fixtures/kr216/src/symbols_asm.s' "$workText/symbols_asm.o"
Compile-From $repoRoot 'tests/fixtures/kr216/src/symbols_asm_helper.c' "$workText/symbols_asm_helper.o" @('-O0')
Link-Fixture 'symbols_asm.kr216' @("$workText/symbols_asm.o", "$workText/symbols_asm_helper.o")

# display.kr216: stdio and input on the shared display. It links the fixture-side
# ABI wrappers (src/display_abi.c) and the SYS trampolines only: no libc and no
# sysroot headers, so the committed image is reproducible on Windows and Linux.
# The fixture also writes a marker to the debug serial console so the adapter
# tests can prove the two devices are distinct.
Compile-From $repoRoot 'tests/fixtures/kr216/src/display_abi.c' "$workText/display_abi.o" @('-O0')
Compile-From $repoRoot 'tests/fixtures/kr216/src/display.c' "$workText/display.o" @('-O0')
Link-Fixture 'display.kr216' @(
    "$workText/crt0.o", "$workText/syscalls.o", "$workText/display_abi.o",
    "$workText/display.o")
Write-Host 'fixtures rebuilt; the adapter tests read them by name'
