# Ships the D1 artifact: the krdbg debugger with the kr232 adapter linked.
#
# The adapter-free release package (scripts/package-windows.ps1) stays the
# UI-golden baseline. This script configures and builds the kr232 release preset
# itself, stages the package, proves the staged executable reports its version
# and loads a kr232 image headless, then writes SHA256SUMS and optionally a zip.
#
# Usage, from a Visual Studio developer shell:
#
#   powershell -File scripts/package-debugger.ps1
#   powershell -File scripts/package-debugger.ps1 -Zip
#   powershell -File scripts/package-debugger.ps1 -Commit <40-character commit>
#
# The compiler package comes from KRG_PREFIX, else KRG_KRC, else
# ../build/family-prefix-release beside this repository. A Release consumer must
# link the Release package: pointing one at the Debug prefix fails on
# __imp__CrtDbgReport (docs/kr-build-instructions.md §6, "two prefixes, not one"),
# so the family default here is the Release prefix the family gate installs.
param(
    [string]$OutputDir = "build/release-kr232",
    [string]$Version = "0.2.0",
    [string]$Commit = "",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$parent = Split-Path -Parent $root
Set-Location $root

function Resolve-CompilerPrefix {
    $prefix = $env:KRG_PREFIX
    if (-not $prefix -and $env:KRG_KRC) {
        $prefix = Split-Path -Parent (Split-Path -Parent $env:KRG_KRC)
    }
    if (-not $prefix) {
        # This package is Release, so prefer the Release prefix the family gate
        # installs; fall back to the single family prefix on a one-prefix machine.
        $release = Join-Path $parent 'build\family-prefix-release'
        $debug = Join-Path $parent 'build\family-prefix'
        if (Test-Path -LiteralPath $release) {
            $prefix = $release
        } elseif (Test-Path -LiteralPath $debug) {
            $prefix = $debug
        }
    }
    if (-not $prefix -or -not (Test-Path -LiteralPath $prefix)) {
        throw "compiler package prefix not found: set KRG_PREFIX or KRG_KRC, or install the Release package into ../build/family-prefix-release"
    }
    return (Resolve-Path -LiteralPath $prefix).Path
}

function Get-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return $null
    }
    $install = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null | Select-Object -First 1)
    if ([string]::IsNullOrWhiteSpace($install)) {
        return $null
    }
    $tools = Get-ChildItem -LiteralPath (Join-Path $install "VC\Tools\MSVC") `
        -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending
    foreach ($tool in $tools) {
        $candidate = Join-Path $tool.FullName "bin\Hostx64\x64\dumpbin.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            $bytes = $sha.ComputeHash($stream)
        } finally {
            $sha.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
    return ([System.BitConverter]::ToString($bytes) -replace "-", "").ToLowerInvariant()
}

foreach ($tool in @('cmake', 'cl')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "'$tool' is not on PATH; start a Visual Studio developer shell (Launch-VsDevShell.ps1) and run this script again"
    }
}

$prefix = Resolve-CompilerPrefix
$kr232 = Join-Path $parent 'kr232'
if (-not (Test-Path -LiteralPath (Join-Path $kr232 'CMakeLists.txt'))) {
    throw "kr232 source tree not found at '$kr232'; the kr232 release preset needs it"
}

$build = Join-Path $root 'build\windows-msvc-kr232-release'
$exe = Join-Path $build 'krdbg_demo.exe'

Write-Host "prefix: $prefix"
Write-Host '== configure windows-msvc-kr232-release'
# --fresh: find_package caches krc-debug's and krc-toolchain's config directories
# in the build tree, so a prefix that changed under a reused directory keeps
# linking the old package. A release package is built from the current prefix.
& cmake --preset windows-msvc-kr232-release --fresh "-DCMAKE_PREFIX_PATH=$prefix"
if ($LASTEXITCODE -ne 0) {
    throw "cmake configure failed (exit code $LASTEXITCODE)"
}

$jobs = if ($env:FAMILY_JOBS) {
    [Math]::Max(1, [int]$env:FAMILY_JOBS)
} else {
    [Environment]::ProcessorCount
}
Write-Host '== build krdbg_demo'
& cmake --build --preset windows-msvc-kr232-release --target krdbg_demo -j $jobs
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed (exit code $LASTEXITCODE)"
}
if (-not (Test-Path -LiteralPath $exe)) {
    throw "the release build did not produce '$exe'"
}

if ($Commit -eq "") {
    $Commit = (& git -C $root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $Commit -eq "") {
        throw "pass -Commit when building from an archive without .git"
    }
}
$vmCommit = (& git -C $kr232 rev-parse HEAD 2>$null | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $vmCommit -eq "") {
    throw "cannot record the kr232 commit: 'git -C $kr232 rev-parse HEAD' failed"
}

$output = Join-Path $root $OutputDir
$name = "krdbg-$Version-windows-x64-kr232"
$stage = Join-Path $output $name
$zipPath = Join-Path $output "$name.zip"

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "third_party\krc-debug") `
    -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage "docs") -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination (Join-Path $stage "krdbg_demo.exe")
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "docs\licenses.md") `
    -Destination (Join-Path $stage "LICENSES.md")
Copy-Item -LiteralPath (Join-Path $root "third_party\fonts\OFL-Inter.txt") `
    -Destination (Join-Path $stage "OFL-Inter.txt")
Copy-Item -LiteralPath (Join-Path $root "third_party\fonts\OFL-JetBrainsMono.txt") `
    -Destination (Join-Path $stage "OFL-JetBrainsMono.txt")
Copy-Item -LiteralPath (Join-Path $root "third_party\krc-debug\LICENSE") `
    -Destination (Join-Path $stage "third_party\krc-debug\LICENSE")
Copy-Item -LiteralPath (Join-Path $root "docs\release.md") `
    -Destination (Join-Path $stage "docs\release.md")
Copy-Item -LiteralPath (Join-Path $root "docs\licenses.md") `
    -Destination (Join-Path $stage "docs\licenses.md")

$required = @(
    "krdbg_demo.exe",
    "README.md",
    "LICENSE",
    "LICENSES.md",
    "OFL-Inter.txt",
    "OFL-JetBrainsMono.txt",
    "third_party\krc-debug\LICENSE",
    "docs\release.md",
    "docs\licenses.md"
)
foreach ($item in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $item))) {
        throw "required release file is missing: $item"
    }
}

# The staged executable, run the way a user runs the artifact.
Write-Host '== staged --version'
Push-Location $stage
try {
    $versionOutput = & ".\krdbg_demo.exe" --version 2>&1
    $versionExit = $LASTEXITCODE
} finally {
    Pop-Location
}
$versionText = ($versionOutput | Out-String).Trim()
if ($versionExit -ne 0) {
    throw "the staged executable failed --version (exit code $versionExit):`n$versionText"
}
if ($versionText -notmatch "krdbg $([regex]::Escape($Version))") {
    throw "the staged executable reports '$versionText', not 'krdbg $Version'; pass -Version to match it"
}

Write-Host '== staged headless kr232 load'
$fixture = Join-Path $root 'tests\fixtures\kr232\loaded.kr232'
Push-Location $stage
try {
    $smokeOutput = & ".\krdbg_demo.exe" --headless --kr232 $fixture 2>&1
    $smokeExit = $LASTEXITCODE
} finally {
    Pop-Location
}
$smokeText = ($smokeOutput | Out-String).Trim()
if ($smokeExit -ne 0) {
    throw "the staged executable failed to load loaded.kr232 (exit code $smokeExit):`n$smokeText"
}
$hashLine = ($smokeOutput | Select-String -Pattern "frame hash").Line
if (-not $hashLine) {
    throw "the staged executable printed no frame hash loading loaded.kr232:`n$smokeText"
}

$dependencies = "dumpbin /dependents: no SDL2.dll"
$dumpbin = Get-Dumpbin
if ($null -eq $dumpbin) {
    throw "dumpbin.exe was not found; cannot verify the release dependencies"
}
$dependents = (& $dumpbin /dependents (Join-Path $stage "krdbg_demo.exe") 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin /dependents failed"
}
if ($dependents -match "SDL2\.dll") {
    throw "the staged executable depends on SDL2.dll"
}
if (Get-ChildItem -LiteralPath $stage -Recurse -Filter "SDL2.dll") {
    throw "SDL2.dll found in the kr232 release package"
}

$verify = @(
    "krdbg $Version windows-x64 (kr232)",
    "Commit: $Commit",
    "VM commit (kr232): $vmCommit",
    "Build: windows-msvc-kr232-release",
    "Compiler prefix: $prefix",
    "Adapters: kr232",
    "Contents: krdbg_demo.exe, README.md, LICENSE, LICENSES.md, OFL-Inter.txt, OFL-JetBrainsMono.txt, third_party/krc-debug/LICENSE, docs/release.md, docs/licenses.md",
    "Version: $versionText",
    "Smoke: headless loaded.kr232 exit 0",
    "Frame: $hashLine",
    "Dependencies: $dependencies",
    "License payload: all required files present",
    "SDL2.dll in package: no",
    "",
    "SHA256SUMS covers every file except SHA256SUMS itself."
)
$verify | Set-Content -LiteralPath (Join-Path $stage "VERIFY.txt") `
    -Encoding utf8

$files = Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object { $_.Name -ne "SHA256SUMS" } |
    Sort-Object FullName
$hashes = foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace("\", "/")
    "$(Get-Sha256 $file.FullName)  $relative"
}
$hashes | Set-Content -LiteralPath (Join-Path $stage "SHA256SUMS") `
    -Encoding ascii

"staged=$stage"
"version=$versionText"
"frame=$hashLine"
if ($Zip) {
    Compress-Archive -LiteralPath $stage -DestinationPath $zipPath `
        -CompressionLevel Optimal
    $size = (Get-Item -LiteralPath $zipPath).Length
    $digest = Get-Sha256 $zipPath
    "artifact=$zipPath"
    "bytes=$size"
    "sha256=$digest"
} else {
    "zip=not requested (pass -Zip to produce $zipPath)"
}

