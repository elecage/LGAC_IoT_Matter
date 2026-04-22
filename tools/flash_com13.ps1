param(
    [string] $IdfPath = "",
    [string] $Port = "COM13"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")
Import-ProjectIdf -IdfPath $IdfPath
Push-Location $Script:RepoRoot
try {
    idf.py -p $Port flash monitor
    if ($LASTEXITCODE -ne 0) {
        throw "idf.py flash monitor failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
