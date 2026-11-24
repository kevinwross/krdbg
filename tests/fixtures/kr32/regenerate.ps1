# Rebuild tests/fixtures/kr32/m3.kr32. Run from the repository root.
$ErrorActionPreference = 'Stop'
$krc = $env:KRDBG_KRC
$krld = $env:KRDBG_KRLD
$sysroot = $env:KRDBG_SYSROOT
$runtime = $env:KRDBG_KR32_RUNTIME
if (-not $krc -or -not $krld -or -not $sysroot -or -not $runtime) {
    throw 'Set KRDBG_KRC, KRDBG_KRLD, KRDBG_SYSROOT and KRDBG_KR32_RUNTIME.'
}
New-Item -ItemType Directory -Force build\m3 | Out-Null
& $krc --target=kr32 --sysroot $sysroot -O0 -c tests\fixtures\kr32\m3.c `
    -o build\m3\m3.kro
if ($LASTEXITCODE -ne 0) { throw "krc failed: $LASTEXITCODE" }
& $krld build\m3\m3.kro "$runtime\crt0.s" tests\fixtures\kr32\getchar.s `
    -o tests\fixtures\kr32\m3.kr32 --base=0x10000 --entry=_start --source-root=.
if ($LASTEXITCODE -ne 0) { throw "krld failed: $LASTEXITCODE" }
Get-FileHash tests\fixtures\kr32\m3.kr32 -Algorithm SHA256 | Format-List
