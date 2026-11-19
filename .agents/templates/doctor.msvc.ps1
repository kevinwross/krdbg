<#
.SYNOPSIS
    Reports whether this machine can build the project with MSVC.

.DESCRIPTION
    Delivered by DevTemplate as a starting point; the project owns this file from here.
    Exit 0 when the machine can build, exit 2 naming each component that is genuinely
    absent.

    The verdict is about the machine, not the current shell. MSVC and the CMake that
    Visual Studio ships are put on PATH by the developer environment, so a check that
    reads only PATH reports a false negative on a correctly provisioned Windows machine.
    When a component is reachable only through that environment, this still passes and
    names the entry point that uses it.

    Read-only by design: it inspects the machine and never installs, edits PATH, writes
    files, or changes configuration. A doctor that repairs things cannot be trusted to
    report the truth about a machine.

.PARAMETER Architecture
    Target architecture to look for under Hostx64. Windows x64 builds use the default.

.EXAMPLE
    .\doctor.msvc.ps1
#>
[CmdletBinding()]
param(
    [string]$Architecture = 'x64'
)

$problems = @()
$installPath = $null
$developerShell = $null

# vswhere is the installer's own index: it is present when Visual Studio or Build Tools is
# installed at all, and its -requires filter is the only reliable test for the C++ workload.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    $problems += "vswhere.exe is not present at $vswhere, so no Visual Studio installation is recorded"
} else {
    $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

    if (-not $installPath) {
        $problems += 'the MSVC C++ build tools are not installed (component Microsoft.VisualStudio.Component.VC.Tools.x86.x64)'
    } else {
        $msvcRoot = Join-Path $installPath 'VC\Tools\MSVC'
        $compiler = Get-ChildItem -LiteralPath $msvcRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin\Hostx64\$Architecture\cl.exe" } |
            Where-Object { Test-Path -LiteralPath $_ } |
            Select-Object -First 1

        if ($compiler) {
            $version = if ($compiler -match '\\MSVC\\([^\\]+)\\') { $matches[1] } else { 'unknown' }
            Write-Output "msvc      $version"
            Write-Output "cl        $compiler"
            Write-Output "studio    $installPath"

            $developerShell = Join-Path $installPath 'VC\Auxiliary\Build\vcvarsall.bat'
            if (-not (Test-Path -LiteralPath $developerShell)) { $developerShell = $null }
        } else {
            $problems += "cl.exe for $Architecture was not found under $msvcRoot"
        }
    }
}

# The SDK ships separately from the compiler, so a working cl.exe says nothing about it.
$sdkInclude = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
if (Test-Path -LiteralPath $sdkInclude) {
    Write-Output "sdk       $sdkInclude"
} else {
    $problems += 'the Windows 10/11 SDK headers are not present (Windows Kits\10\Include)'
}

# CMake: on PATH, or the copy Visual Studio installs for its own use. The second is a pass
# with an instruction rather than a failure, because the machine builds either way.
$cmake = $null
$cmakeVia = $null
$onPath = Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if ($onPath) {
    $cmake = $onPath.Source
    $cmakeVia = 'path'
} elseif ($installPath) {
    $bundled = Join-Path $installPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (Test-Path -LiteralPath $bundled) {
        $cmake = $bundled
        $cmakeVia = 'developer environment'
    }
}

if ($cmake) {
    $cmakeVersion = (& $cmake --version 2>$null | Select-Object -First 1)
    Write-Output ("cmake     {0} ({1})" -f $cmakeVersion, $cmakeVia)
} else {
    $problems += 'cmake is neither on PATH nor provided by Visual Studio'
}

if ($problems.Count -gt 0) {
    foreach ($problem in $problems) { [Console]::Error.WriteLine("missing   $problem") }
    [Console]::Error.WriteLine('')
    [Console]::Error.WriteLine('doctor: this machine cannot build yet. Do not install or substitute anything')
    [Console]::Error.WriteLine('        without approval; report the missing component instead.')
    exit 2
}

Write-Output ''
if ($cmakeVia -eq 'developer environment') {
    Write-Output 'doctor: this machine can build, but only from the Visual Studio developer environment'
    if ($developerShell) {
        Write-Output "        initialise it with: `"$developerShell`" $Architecture"
        Write-Output '        or run the project build entry point, which does that for you'
    }
} else {
    Write-Output "doctor: this machine can build ($Architecture) with the toolchain above"
}
exit 0
