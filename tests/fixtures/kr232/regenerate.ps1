# Rebuilds the kr232 fixtures from source with M2's toolchain.
#
# The containers are checked in because the adapter tests must run without a
# compiler, but they are not hand-made: this script is how they are produced.
# Set KR232_SOURCE to the kr232 checkout and KRC/KRLD to the compiler's tools.
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

# crt0 is the entry every fixture links with.
& $krc -c --target kr232 (Join-Path $sources 'targets\kr232\runtime\crt0.s') -o (Join-Path $work 'crt0.o')
if ($LASTEXITCODE -ne 0) { throw 'crt0 failed' }

function Build-Fixture([string] $name, [string[]] $objects) {
    $inputs = @((Join-Path $work 'crt0.o')) + $objects
    & $krld "--source-root=$sources" --entry=_start @inputs -o (Join-Path $PSScriptRoot $name)
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
    Write-Host "wrote $name"
}

# loaded.k232: the two-instruction program the layout and memory tests use.
& $krc -c --target kr232 (Join-Path $sources 'tests\fixtures\c\kr232_min.c') -o (Join-Path $work 'min.o')
Build-Fixture 'loaded.k232' @((Join-Path $work 'min.o'))

# nested_frames.k232: two frames on the stack for the unwind test.
& $krc -c --target kr232 (Join-Path $sources 'tests\fixtures\c\kr232_nested_frames.c') -o (Join-Path $work 'nested.o')
Build-Fixture 'nested_frames.k232' @((Join-Path $work 'nested.o'))

# session.k232: a console byte and a committed frame for the device tests.
$include = Join-Path $sources 'sysroot\kr232\include'
& $krc -c --target kr232 -I $include (Join-Path $sources 'tests\fixtures\c\kr232_session.c') -o (Join-Path $work 'session.o')
& $krc -c --target kr232 -I $include (Join-Path $sources 'targets\kr232\runtime\cons.c') -o (Join-Path $work 'cons.o')
& $krc -c --target kr232 -I $include (Join-Path $sources 'targets\kr232\runtime\gfx.c') -o (Join-Path $work 'gfx.o')
Build-Fixture 'session.k232' @((Join-Path $work 'session.o'), (Join-Path $work 'cons.o'), (Join-Path $work 'gfx.o'))

# kernel.k232 is the kernel the adapter runs user programs under, and
# userprint.k232 is a program that prints through it and exits 3.
& $krc -c --target kr232 (Join-Path $kr232 'targets\kr232\kernel\kernel.s') -o (Join-Path $work 'kernel.o')
if ($LASTEXITCODE -ne 0) { throw 'kernel failed' }
& $krld "--source-root=$kr232" --entry=_start (Join-Path $work 'kernel.o') -o (Join-Path $PSScriptRoot 'kernel.k232')
if ($LASTEXITCODE -ne 0) { throw 'kernel.k232 failed' }
Write-Host 'wrote kernel.k232'

& $krc -c --target kr232 (Join-Path $PSScriptRoot 'userprint.s') -o (Join-Path $work 'userprint.o')
if ($LASTEXITCODE -ne 0) { throw 'userprint failed' }
& $krld "--source-root=$PSScriptRoot" --base=0x08000000 --entry=_start (Join-Path $work 'userprint.o') -o (Join-Path $PSScriptRoot 'userprint.k232')
if ($LASTEXITCODE -ne 0) { throw 'userprint.k232 failed' }
Write-Host 'wrote userprint.k232'
Write-Host 'fixtures rebuilt; the adapter tests read them by name'
