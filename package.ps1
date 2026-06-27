<#
.SYNOPSIS
    Builds AxiSim and assembles a self-contained, redistributable ZIP.

.DESCRIPTION
    1. Sets up the MSVC build environment (via vswhere) unless -SkipBuild.
    2. Builds the AxiSim target (Ninja / RelWithDebInfo).
    3. Runs `cmake --install` into a staging folder, gathering the exe,
       glfw3.dll, gmsh*.dll, the MSVC runtime DLLs, and the assets/ and
       graphics/shaders/ folders.
    4. Compresses the staging folder into dist\<name>.zip ready to attach
       to a GitHub Release.

    NOTE: AxiSim is CUDA software. The end user still needs an NVIDIA GPU
    (compute capability >= 7.5) with a recent driver -- that cannot be bundled.

.PARAMETER Version
    Version tag appended to the package name, e.g. -Version v1.04-alpha
    produces dist\AxiSim-v1.04-alpha\ and dist\AxiSim-v1.04-alpha.zip.

.PARAMETER BuildDir
    CMake build directory to install from. Default: out\build\x64-Release.

.PARAMETER SkipBuild
    Skip the compile step and package whatever is already built. Use this
    if you just built in Visual Studio.

.EXAMPLE
    .\package.ps1 -Version v1.04-alpha
#>
[CmdletBinding()]
param(
    [string]$Version  = "",
    [string]$BuildDir = "out\build\x64-Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

$buildPath = Join-Path $root $BuildDir
if (-not (Test-Path $buildPath)) {
    throw "Build directory not found: $buildPath`nConfigure the project once (e.g. open it in Visual Studio) before packaging."
}

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio, or run this from a Developer PowerShell and pass -SkipBuild."
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vsPath) { throw "No Visual Studio installation with MSBuild found." }
    return $vsPath
}

function Enter-DevShell {
    $vsPath = Get-VsInstallPath
    $devShell = Join-Path $vsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
    Set-Location $root
}

function Resolve-CMake {
    $c = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if ($c) { return $c }
    # Fall back to the CMake bundled with Visual Studio, then a standalone install.
    try { $vsPath = Get-VsInstallPath } catch { $vsPath = $null }
    $cands = @()
    if ($vsPath) {
        $cands += Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    $cands += Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
    $hit = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $hit) { throw "cmake.exe not found on PATH or in Visual Studio. Install CMake or run from a Developer PowerShell." }
    return $hit
}

# --- 1 & 2: build -----------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "==> Setting up MSVC environment..." -ForegroundColor Cyan
    Enter-DevShell
    $cmake = Resolve-CMake
    Write-Host "==> Building AxiSim..." -ForegroundColor Cyan
    & $cmake --build $buildPath --target AxiSim
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
} else {
    Write-Host "==> Skipping build (-SkipBuild)." -ForegroundColor Yellow
    $cmake = Resolve-CMake
}

# --- 3: install into staging folder ----------------------------------------
$pkgName   = if ($Version) { "AxiSim-$Version" } else { "AxiSim" }
$distDir   = Join-Path $root "dist"
$stagePath = Join-Path $distDir $pkgName

if (Test-Path $stagePath) { Remove-Item -Recurse -Force $stagePath }
New-Item -ItemType Directory -Force -Path $stagePath | Out-Null

Write-Host "==> Assembling package in $stagePath ..." -ForegroundColor Cyan
& $cmake --install $buildPath --prefix $stagePath
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed (exit $LASTEXITCODE)." }

# --- sanity check: the runtime asset folders must be present ----------------
foreach ($req in @("AxiSim.exe", "assets\fonts\Roboto-Regular.ttf", "graphics\shaders\mesh.vert")) {
    if (-not (Test-Path (Join-Path $stagePath $req))) {
        throw "Package is missing required file: $req"
    }
}

# --- 4: zip -----------------------------------------------------------------
$zipPath = Join-Path $distDir "$pkgName.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }

Write-Host "==> Compressing to $zipPath ..." -ForegroundColor Cyan
Compress-Archive -Path $stagePath -DestinationPath $zipPath

$sizeMB = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
Write-Host ""
Write-Host "Done. Package ready:" -ForegroundColor Green
Write-Host "  Folder: $stagePath"
Write-Host "  Zip:    $zipPath  ($sizeMB MB)"
Write-Host ""
Write-Host "Distribute via a GitHub Release, e.g.:" -ForegroundColor Cyan
$tag = if ($Version) { $Version } else { "vX.Y" }
Write-Host "  gh release create $tag `"$zipPath`" --title `"AxiSim $tag`" --notes `"Requires an NVIDIA GPU (compute capability >= 7.5) and a recent driver.`""
