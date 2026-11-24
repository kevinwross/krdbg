# Rebuilds the kr232 fixtures from source with M2's toolchain.
#
# The containers are checked in because the adapter tests must run without a
# compiler, but they are not hand-made: this script is how they are produced.
# Set KR232_SOURCE to the kr232 checkout and KRC/KRLD to the compiler's tools.
#
# Every path handed to krc and krld uses forward slashes, because the source path
# is embedded in the image: a recording made with backslashes produced an image
# `CheckFixtureDrift.cmake`, which passes forward slashes, could not reproduce.
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

# The spelling every tool sees, so a recording matches what the check builds.
$sourcesText = (Resolve-Path $sources).Path -replace '\\', '/'
$kr232Text = (Resolve-Path $kr232).Path -replace '\\', '/'
$fixtureText = (Resolve-Path $PSScriptRoot).Path -replace '\\', '/'
$workText = (Resolve-Path $work).Path -replace '\\', '/'
$includeText = "$sourcesText/sysroot/kr232/include"

# crt0 is the entry every fixture links with.
& $krc -c --target kr232 "$sourcesText/targets/kr232/runtime/crt0.s" -o "$workText/crt0.o"
if ($LASTEXITCODE -ne 0) { throw 'crt0 failed' }

function Build-Fixture([string] $name, [string[]] $objects) {
    $inputs = @("$workText/crt0.o") + $objects
    & $krld "--source-root=$sourcesText" --entry=_start @inputs -o "$fixtureText/$name"
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
    Write-Host "wrote $name"
}

# loaded.kr232: the two-instruction program the layout and memory tests use.
& $krc -c --target kr232 "$sourcesText/tests/fixtures/c/kr232_min.c" -o "$workText/min.o"
Build-Fixture 'loaded.kr232' @("$workText/min.o")

# nested_frames.kr232: two frames on the stack for the unwind test.
& $krc -c --target kr232 "$sourcesText/tests/fixtures/c/kr232_nested_frames.c" -o "$workText/nested.o"
Build-Fixture 'nested_frames.kr232' @("$workText/nested.o")

# session.kr232: a console byte and a committed frame for the device tests.
& $krc -c --target kr232 -I $includeText "$sourcesText/tests/fixtures/c/kr232_session.c" -o "$workText/session.o"
& $krc -c --target kr232 -I $includeText "$sourcesText/targets/kr232/runtime/cons.c" -o "$workText/cons.o"
& $krc -c --target kr232 -I $includeText "$sourcesText/targets/kr232/runtime/gfx.c" -o "$workText/gfx.o"
Build-Fixture 'session.kr232' @("$workText/session.o", "$workText/cons.o", "$workText/gfx.o")

# kernel.kr232 is the kernel the adapter runs user programs under, and
# userprint.kr232 is a program that prints through it and exits 3.
& $krc -c --target kr232 "$kr232Text/targets/kr232/kernel/kernel.s" -o "$workText/kernel.o"
if ($LASTEXITCODE -ne 0) { throw 'kernel failed' }
& $krld "--source-root=$kr232Text" --entry=_start "$workText/kernel.o" -o "$fixtureText/kernel.kr232"
if ($LASTEXITCODE -ne 0) { throw 'kernel.kr232 failed' }
Write-Host 'wrote kernel.kr232'

& $krc -c --target kr232 "$fixtureText/userprint.s" -o "$workText/userprint.o"
if ($LASTEXITCODE -ne 0) { throw 'userprint failed' }
& $krld "--source-root=$fixtureText" --base=0x08000000 --entry=_start "$workText/userprint.o" -o "$fixtureText/userprint.kr232"
if ($LASTEXITCODE -ne 0) { throw 'userprint.kr232 failed' }
Write-Host 'wrote userprint.kr232'
Write-Host 'fixtures rebuilt; the adapter tests read them by name'
