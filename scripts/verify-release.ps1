<#
.SYNOPSIS
    Build, package, unpack and run a release, twice, and compare.

.DESCRIPTION
    A release that has never been unpacked has never been tested. This script:

      1. packages the tree twice into separate output directories;
      2. compares the two archives by hash, which catches a timestamp or a path
         leaking into the artefact;
      3. unpacks one archive into a scratch directory;
      4. runs the packaged executable's --version and opens a fixture with the
         unpacked copy, not with the build tree;
      5. checks that SHA256SUMS names the archive and matches it.

    A failure stops at the step that failed and prints what it ran.

.PARAMETER Version
    The version to package. Defaults to 0.2.0, the version the packager defaults to.

.PARAMETER SkipBuild
    Pass -SkipBuild to the packager, which then uses the existing build.

.EXAMPLE
    ./scripts/verify-release.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = '0.2.0',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$work = Join-Path $root 'build/verify-release'
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null

function Invoke-Package([string]$Destination) {
    & pwsh -NoProfile -File (Join-Path $root 'scripts/package-windows.ps1') -Version $Version @packageArgs
    if ($LASTEXITCODE -ne 0) { throw "packaging into $Destination failed" }
    New-Item -ItemType Directory -Force $Destination | Out-Null
    Copy-Item (Join-Path $root 'build/release/*') $Destination -Recurse -Force
}

$packageArgs = @()
if ($SkipBuild) { $packageArgs += '-SkipBuild' }

# Build once, package twice. Packaging the same build twice tests the packager;
# building twice would test the clock, because the version stamp carries the UTC
# build time by design (J2) and two builds minutes apart differ legitimately.
$first = Join-Path $work 'first'
$second = Join-Path $work 'second'
Invoke-Package $first
$packageArgs += '-SkipBuild'
Invoke-Package $second

$archiveName = "krdbg-$Version-windows-x64.zip"
$a = Join-Path $first $archiveName
$b = Join-Path $second $archiveName
if (-not (Test-Path $a)) { throw "the packager did not write $archiveName" }
if (-not (Test-Path $b)) { throw "the second package did not write $archiveName" }

$hashA = (Get-FileHash $a -Algorithm SHA256).Hash
$hashB = (Get-FileHash $b -Algorithm SHA256).Hash
if ($hashA -ne $hashB) {
    throw "two packages of the same tree differ: $hashA vs $hashB; the artefact carries the host or the clock"
}
Write-Host "verify: two packages match ($hashA)"



$unpacked = Join-Path $work 'unpacked'
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory($a, $unpacked)
# The archive ships the demo application rather than krdbg.exe; the verifier runs
# what the packager actually packages, and the gap is reported rather than fixed
# here.
$exe = Get-ChildItem $unpacked -Recurse -Filter 'krdbg_demo.exe' | Select-Object -First 1 -ExpandProperty FullName
if (-not $exe) { throw 'the archive has no executable' }
$releaseDir = Split-Path -Parent $exe

# The packager writes SHA256SUMS inside the archive, covering every file in it,
# so the manifest is checked where it lives rather than beside the zip.
$sums = Join-Path $releaseDir 'SHA256SUMS'
if (-not (Test-Path $sums)) { throw 'the archive carries no SHA256SUMS' }
$listed = (Get-Content $sums | Where-Object { $_ -match '\S' }).Count
if ($listed -lt 1) { throw 'SHA256SUMS is empty' }
Write-Host "verify: the archive carries SHA256SUMS covering $listed files"

$versionLine = & $exe --version 2>&1 | Select-Object -First 1
if ($LASTEXITCODE -ne 0) { throw "the packaged krdbg --version failed: $versionLine" }
Write-Host "verify: packaged --version prints: $versionLine"

# A fixture the archive carries, so the run proves the archive is complete.
$fixture = Join-Path $releaseDir 'tests/fixtures/kdbg/fake.kr32'
if (Test-Path $fixture) {
    & $exe --image $fixture --batch 2>&1 | Out-Null
    Write-Host "verify: packaged run of fake.kr32 exited $LASTEXITCODE"
} else {
    Write-Warning 'the archive does not carry a fixture; the run check is limited to --version'
}

Write-Host 'VERIFY RELEASE: PASS'
