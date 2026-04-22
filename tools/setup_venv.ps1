param(
    [switch] $UpgradePip
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$VenvPython = Join-Path $RepoRoot ".venv\Scripts\python.exe"

if (-not (Test-Path $VenvPython)) {
    python -m venv (Join-Path $RepoRoot ".venv")
}

if ($UpgradePip) {
    & $VenvPython -m pip install --upgrade pip
}
& $VenvPython -m pip install -r (Join-Path $RepoRoot "requirements.txt")

Write-Host "venv ready: $VenvPython"
