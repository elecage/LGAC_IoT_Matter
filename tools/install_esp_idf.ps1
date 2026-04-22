param(
    [string] $Version = "v5.4.1",
    [string] $InstallPath = "",
    [switch] $NoRecursiveClone
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallPath) {
    $InstallPath = Join-Path $RepoRoot ".esp-idf"
}

$InstallPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($InstallPath)
$Parent = Split-Path -Parent $InstallPath

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found in PATH. Install Git for Windows before running this script."
}

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw "python was not found in PATH. Install Python 3 before running this script."
}

if (-not (Test-Path $Parent)) {
    New-Item -ItemType Directory -Force -Path $Parent | Out-Null
}

if (-not (Test-Path $InstallPath)) {
    Write-Host "Cloning ESP-IDF $Version into $InstallPath"
    if ($NoRecursiveClone) {
        git clone --depth 1 --branch $Version https://github.com/espressif/esp-idf.git $InstallPath
    }
    else {
        git clone --depth 1 --branch $Version --recursive https://github.com/espressif/esp-idf.git $InstallPath
    }
}
else {
    Write-Host "ESP-IDF directory already exists: $InstallPath"
}

Push-Location $InstallPath
try {
    if ($NoRecursiveClone) {
        git submodule update --init --recursive
    }

    Write-Host "Installing ESP-IDF tools for ESP32-C3"
    .\install.ps1 esp32c3
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "ESP-IDF is ready."
Write-Host "Build this project with:"
Write-Host "  .\tools\build.ps1 -IdfPath `"$InstallPath`""
Write-Host "Flash COM13 with:"
Write-Host "  .\tools\flash_com13.ps1 -IdfPath `"$InstallPath`""

