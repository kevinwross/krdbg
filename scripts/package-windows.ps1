param(
    [string]$BuildDir = "build/windows-msvc-release",
    [string]$OutputDir = "build/release",
    [string]$Version = "0.1.0",
    [string]$Commit = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$output = Join-Path $root $OutputDir
$name = "krdbg-$Version-windows-x64"
$stage = Join-Path $output $name
$zip = Join-Path $output "$name.zip"

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

$exe = Join-Path $build "krdbg_demo.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "missing release executable: $exe"
}
if ($Commit -eq "") {
    $Commit = (& git -C $root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $Commit -eq "") {
        throw "pass -Commit when building from an archive without .git"
    }
}

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination (Join-Path $stage "krdbg_demo.exe")
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $stage
Copy-Item -LiteralPath (Join-Path $root "docs\licenses.md") `
    -Destination (Join-Path $stage "LICENSES.md")
Copy-Item -LiteralPath (Join-Path $root "third_party\krc-debug\LICENSE") `
    -Destination (Join-Path $stage "LICENSES-krc-debug.txt")
Copy-Item -LiteralPath (Join-Path $root "docs\release.md") -Destination $stage
New-Item -ItemType Directory -Path (Join-Path $stage "docs") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root "docs\release.md") `
    -Destination (Join-Path $stage "docs\release.md")
Copy-Item -LiteralPath (Join-Path $root "docs\licenses.md") `
    -Destination (Join-Path $stage "docs\licenses.md")

$smoke = Join-Path $stage "smoke-loaded.ppm"
Push-Location $stage
try {
    $smokeOutput = & ".\krdbg_demo.exe" --headless --state loaded `
        --screenshot "smoke-loaded.ppm" 2>&1
    $smokeExit = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($smokeExit -ne 0) {
    throw "staged executable failed its headless smoke test"
}
$hashLine = ($smokeOutput | Select-String -Pattern "frame hash").Line
Remove-Item -LiteralPath $smoke -Force -ErrorAction SilentlyContinue

$dependencies = "dumpbin not available; dependency scan not run"
$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($null -ne $dumpbin) {
    $dependencies = (& $dumpbin.Source /dependents $exe 2>&1 | Out-String)
    if ($dependencies -match "SDL2\.dll") {
        throw "release executable depends on SDL2.dll"
    }
    $dependencies = "dumpbin /dependents: no SDL2.dll"
}

$verify = @(
    "krdbg $Version windows-x64",
    "Commit: $Commit",
    "Build: windows-msvc-release",
    "Contents: krdbg_demo.exe, README.md, LICENSES.md, LICENSES-krc-debug.txt, release.md, docs/release.md, docs/licenses.md",
    "Smoke: headless loaded state exit 0",
    "Frame: $hashLine",
    "Dependencies: $dependencies",
    "SDL2.dll in package: no",
    "",
    "SHA256SUMS covers every file except SHA256SUMS itself."
)
$verify | Set-Content -LiteralPath (Join-Path $stage "VERIFY.txt") `
    -Encoding utf8

$files = Get-ChildItem -LiteralPath $stage -File |
    Where-Object { $_.Name -ne "SHA256SUMS" } |
    Sort-Object Name
$hashes = foreach ($file in $files) {
    "$(Get-Sha256 $file.FullName)  $($file.Name)"
}
$hashes | Set-Content -LiteralPath (Join-Path $stage "SHA256SUMS") `
    -Encoding ascii

if (Get-ChildItem -LiteralPath $stage -Recurse -Filter "SDL2.dll") {
    throw "SDL2.dll found in the release package"
}

Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
$size = (Get-Item -LiteralPath $zip).Length
$digest = Get-Sha256 $zip
"artifact=$zip"
"bytes=$size"
"sha256=$digest"
