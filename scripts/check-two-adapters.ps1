# Builds the debugger with two IDebugTarget adapters enabled and runs the whole
# unit suite.
#
# The render goldens must not depend on the linked layout: adding an adapter
# shifts every address in the binary, and the surface and golden tests compare
# pixels. Running with two adapters at once is what keeps that true, and 109 of
# 109 tests must pass.
#
# Run from a Visual Studio developer shell (the gate scripts initialize one).
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build/two-adapters'

cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Debug `
    -DKRDBG_KR216_ADAPTER=ON -DKRDBG_KR232_ADAPTER=ON
if ($LASTEXITCODE -ne 0) { throw "configuring two adapters failed" }

cmake --build $build --target krdbg_tests
if ($LASTEXITCODE -ne 0) { throw "building the two-adapter test binary failed" }

$tests = Join-Path $build 'krdbg_tests.exe'
$output = & $tests 2>&1
$summary = ($output | Select-Object -Last 1)
$output | Select-String -Pattern '\[ FAIL \]' | ForEach-Object { $_.Line }
Write-Output $summary
if ($summary -notmatch '0 failure') { throw "the two-adapter suite failed: $summary" }
