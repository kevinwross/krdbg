# Rebuild the KR16 M4 fixtures. Run from the repository root.
$ErrorActionPreference = 'Stop'
$krc = $env:KRDBG_KRC
$kr16 = $env:KRDBG_KR16
if (-not $krc -or -not $kr16) {
    throw 'Set KRDBG_KRC and KRDBG_KR16.'
}
New-Item -ItemType Directory -Force build\m4 | Out-Null
& $krc --target=kr16 -O0 -c tests\fixtures\kr16\m4.c `
    -o tests\fixtures\kr16\m4.kr16
if ($LASTEXITCODE -ne 0) { throw "krc failed: $LASTEXITCODE" }
& $kr16 cc -g -O0 tests\fixtures\kr16\m4.c -o build\m4\legacy.o
if ($LASTEXITCODE -ne 0) { throw "kr16 cc failed: $LASTEXITCODE" }
& $kr16 ld build\m4\legacy.o -o tests\fixtures\kr16\legacy.kr16
if ($LASTEXITCODE -ne 0) { throw "kr16 ld failed: $LASTEXITCODE" }
Get-FileHash tests\fixtures\kr16\m4.kr16, tests\fixtures\kr16\legacy.kr16 `
    -Algorithm SHA256 | Format-List
