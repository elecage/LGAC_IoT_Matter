param(
    [string] $IdfPath = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")
Import-ProjectIdf -IdfPath $IdfPath
Push-Location $Script:RepoRoot
try {
    if (-not (Test-Path (Join-Path $Script:RepoRoot "sdkconfig"))) {
        idf.py set-target esp32c3
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py set-target failed with exit code $LASTEXITCODE"
        }
    }
    idf.py build
    if ($LASTEXITCODE -ne 0) {
        throw "idf.py build failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
