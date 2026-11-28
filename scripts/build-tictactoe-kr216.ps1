# Builds KRCompiler's tictactoe example for kr216 into the transient build tree.
#
#   build/tictactoe-kr216/tictactoe.kr216
#
# The recipe is the one the kr216 runtime documents: compile the C++ example and
# the runtime (crt0, syscalls, eh, libc and the display wrappers) with the same
# compiler, then link with --entry=_start. There are no getchar/rand shims: the
# program uses the real stdio path, which task-24 routes to the text display.
#
# The image is transient, like build/startrekng-kr232; CTest registers the
# tictactoe DISPLAY row only when the image exists, so a clean checkout without
# the script still builds and tests.
#
# Usage, from this repository:
#   powershell -File scripts/build-tictactoe-kr216.ps1
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix beside this repository. KRCOMPILER_ROOT overrides the
# sibling KRCompiler tree (default: ../krcompiler).
[CmdletBinding()]
param(
    [string]$KrCompilerRoot = $env:KRCOMPILER_ROOT,
    [string]$Prefix = $env:KRG_PREFIX,
    [string]$BuildDirectory = 'build/tictactoe-kr216',
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
$runtime = Join-Path $KrCompilerRoot 'targets\kr216\runtime'
foreach ($required in @($krc, $krld, $runtime)) {
    if (-not (Test-Path $required)) {
        throw "missing '$required'; the prefix or the KRCompiler tree is incomplete"
    }
}

$output = Join-Path $repo $BuildDirectory
if ($Clean -and (Test-Path $output)) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $output | Out-Null

Push-Location $KrCompilerRoot
try {
    $objects = @()
    foreach ($unit in @(
            @{ Source = 'examples/tictactoe.cpp'; Object = 'tictactoe.o' },
            @{ Source = 'targets/kr216/runtime/crt0.s'; Object = 'crt0.o' },
            @{ Source = 'targets/kr216/runtime/syscalls.s'; Object = 'syscalls.o' },
            @{ Source = 'targets/kr216/runtime/eh.s'; Object = 'eh.o' },
            @{ Source = 'targets/kr216/runtime/libc.c'; Object = 'libc.o' },
            @{ Source = 'targets/kr216/runtime/display.c'; Object = 'display.o' })) {
        $object = Join-Path $output $unit.Object
        if ($unit.Object -eq 'tictactoe.o') {
            & $krc -c --target kr216 -O0 $unit.Source -o $object
        } else {
            & $krc -c --target kr216 $unit.Source -o $object
        }
        if ($LASTEXITCODE -ne 0) {
            throw "krc failed for $($unit.Source)"
        }
        $objects += $object
    }
    $image = Join-Path $output 'tictactoe.kr216'
    & $krld --entry=_start @objects -o $image
    if ($LASTEXITCODE -ne 0) {
        throw "krld failed for $image"
    }
    Write-Host "wrote $image"
} finally {
    Pop-Location
}

$krcVersion = (& $krc --version 2>&1 | Out-String).Trim()
$info = @(
    "source: $KrCompilerRoot/examples/tictactoe.cpp",
    "prefix: $Prefix",
    "krc: $krcVersion",
    "built: $(Get-Date -Format o)"
)
Set-Content -LiteralPath (Join-Path $output 'build-info.txt') -Value $info
Write-Host "tictactoe kr216 build in $output"
