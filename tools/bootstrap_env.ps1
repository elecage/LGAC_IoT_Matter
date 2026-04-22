param(
    [string] $IdfPath = "",
    [switch] $InstallEspIdf,
    [switch] $Build,
    [switch] $Flash
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot

if ($InstallEspIdf) {
    $InstallArgs = @{}
    if ($IdfPath) {
        $InstallArgs.InstallPath = $IdfPath
    }
    & (Join-Path $PSScriptRoot "install_esp_idf.ps1") @InstallArgs
}

& (Join-Path $PSScriptRoot "setup_venv.ps1")

. (Join-Path $PSScriptRoot "common.ps1")
Import-ProjectIdf -IdfPath $IdfPath

Push-Location $RepoRoot
try {
    idf.py set-target esp32c3
    if ($LASTEXITCODE -ne 0) {
        throw "idf.py set-target failed with exit code $LASTEXITCODE"
    }

    if ($Build) {
        idf.py build
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py build failed with exit code $LASTEXITCODE"
        }
    }

    if ($Flash) {
        idf.py -p COM13 flash monitor
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py flash monitor failed with exit code $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}

Write-Host ""
Write-Host "Environment is ready for this script session."
Write-Host "Use .\tools\build.ps1 for builds, or .\tools\flash_com13.ps1 to flash COM13."
