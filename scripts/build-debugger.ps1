# Builds the debugger with the kr232 adapter linked and proves it loads an image.
#
# One command from a Visual Studio developer shell:
#
#   powershell -File scripts/build-debugger.ps1
#   powershell -File scripts/build-debugger.ps1 -Clean
#   powershell -File scripts/build-debugger.ps1 -BuildDirectory build/my-debugger
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix beside this repository. The kr232 and KRCompiler trees
# are siblings of this repository; the script stops before configuring when
# either is missing, because the kr232 preset points at that exact path and the
# smoke loads a fixture whose sources resolve only under ../krcompiler.
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/kr232-debugger',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$parent = Split-Path -Parent $repo
Set-Location $repo

function Resolve-CompilerPrefix {
    $prefix = $env:KRG_PREFIX
    if (-not $prefix -and $env:KRG_KRC) {
        $prefix = Split-Path -Parent (Split-Path -Parent $env:KRG_KRC)
    }
    if (-not $prefix) {
        $candidate = Join-Path $parent 'build\family-prefix'
        if (Test-Path -LiteralPath $candidate) {
            $prefix = $candidate
        }
    }
    if (-not $prefix -or -not (Test-Path -LiteralPath $prefix)) {
        throw "compiler package prefix not found: set KRG_PREFIX or KRG_KRC, or install the package into ../build/family-prefix"
    }
    $resolved = (Resolve-Path -LiteralPath $prefix).Path
    $configs = @(
        'bin\krc.exe',
        'lib\cmake\krc-debug\krc-debug-config.cmake',
        'lib\cmake\krc-toolchain\krc-toolchain-config.cmake'
    )
    foreach ($relative in $configs) {
        if (-not (Test-Path -LiteralPath (Join-Path $resolved $relative))) {
            throw "the compiler package at '$resolved' is incomplete: missing '$relative'"
        }
    }
    return $resolved
}

foreach ($tool in @('cmake', 'cl')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "'$tool' is not on PATH; start a Visual Studio developer shell (Launch-VsDevShell.ps1) and run this script again"
    }
}

$prefix = Resolve-CompilerPrefix
$kr232 = Join-Path $parent 'kr232'
$krcompiler = Join-Path $parent 'krcompiler'
if (-not (Test-Path -LiteralPath (Join-Path $kr232 'CMakeLists.txt'))) {
    throw "kr232 source tree not found at '$kr232'; the kr232 preset and the adapter need it"
}
if (-not (Test-Path -LiteralPath $krcompiler)) {
    throw "KRCompiler tree not found at '$krcompiler'; the smoke's --source-root and the fixtures' sources need it"
}

$build = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repo $BuildDirectory
}
$build = [System.IO.Path]::GetFullPath($build)
$repoFull = [System.IO.Path]::GetFullPath($repo).TrimEnd('\', '/')
$separator = [System.IO.Path]::DirectorySeparatorChar
if ($Clean -and (Test-Path -LiteralPath $build)) {
    if (-not $build.StartsWith($repoFull + $separator, [StringComparison]::OrdinalIgnoreCase)) {
        throw "-Clean refuses to delete '$build': it is not inside the repository '$repo'"
    }
    Write-Host "clean $build"
    Remove-Item -LiteralPath $build -Recurse -Force
}

$jobs = if ($env:FAMILY_JOBS) {
    [Math]::Max(1, [int]$env:FAMILY_JOBS)
} else {
    [Environment]::ProcessorCount
}

Write-Host "prefix: $prefix"
Write-Host "kr232: $kr232"
Write-Host "== configure windows-msvc-kr232 in $build"
& cmake --preset windows-msvc-kr232 -B $build "-DCMAKE_PREFIX_PATH=$prefix"
if ($LASTEXITCODE -ne 0) {
    throw "cmake configure failed (exit code $LASTEXITCODE)"
}

Write-Host '== build the debugger, tests and the kr232 tools'
& cmake --build $build -j $jobs
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed (exit code $LASTEXITCODE)"
}

$exe = Join-Path $build 'krdbg_demo.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "the build did not produce '$exe'"
}

# The documented smoke, run from the repository root because the source root and
# the fixture are named relative to it.
Write-Host '== smoke: load tests/fixtures/kr232/loaded.kr232 headless'
$smoke = & $exe --headless --kr232 'tests/fixtures/kr232/loaded.kr232' --source-root '../krcompiler' 2>&1
$smokeExit = $LASTEXITCODE
$smokeText = ($smoke | Out-String).Trim()
if ($smokeExit -ne 0) {
    throw "the headless kr232 smoke failed (exit code $smokeExit):`n$smokeText"
}
if ($smokeText -notmatch 'frame hash [0-9A-Fa-f]{16}') {
    throw "the headless kr232 smoke printed no frame hash:`n$smokeText"
}

Write-Host "binary: $exe"
Write-Host "smoke: $($Matches[0])"
Write-Host 'BUILD DEBUGGER: PASS'
