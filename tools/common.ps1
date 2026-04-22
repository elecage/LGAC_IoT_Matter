$ErrorActionPreference = "Stop"

$Script:RepoRoot = Split-Path -Parent $PSScriptRoot
$Script:DefaultIdfPath = Join-Path $Script:RepoRoot ".esp-idf"

function Get-ProjectIdfPath {
    param(
        [string] $IdfPath
    )

    if ($IdfPath) {
        return $IdfPath
    }

    if ($env:IDF_PATH) {
        return $env:IDF_PATH
    }

    if (Test-Path (Join-Path $Script:DefaultIdfPath "export.ps1")) {
        return $Script:DefaultIdfPath
    }

    return $null
}

function Import-ProjectIdf {
    param(
        [string] $IdfPath
    )

    $ResolvedIdfPath = Get-ProjectIdfPath -IdfPath $IdfPath
    if (-not $ResolvedIdfPath) {
        throw "ESP-IDF is not configured. Run .\tools\install_esp_idf.ps1 first, or pass -IdfPath to this script."
    }

    $ExportScript = Join-Path $ResolvedIdfPath "export.ps1"
    if (-not (Test-Path $ExportScript)) {
        throw "ESP-IDF export script was not found: $ExportScript"
    }

    . $ExportScript
}
